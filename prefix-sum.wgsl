@group(0) @binding(0) var<storage, read_write> in: array<u32>;
@group(0) @binding(1) var<storage, read_write> prefix_states: array<atomic<u32>>;
@group(0) @binding(2) var<storage, read_write> out: array<u32>;
@group(0) @binding(3) var<storage, read_write> part: atomic<u32>;
@group(0) @binding(4) var<storage, read_write> debug: array<u32>;

const BATCH_SIZE = 4;
const FLG_A = 1;
const FLG_P = 2;
const ANTI_MASK = 30u;
const MASK_ = ~(3u << ANTI_MASK);

override wg_size: u32;

var<workgroup> wg_broadcast: u32;
var<workgroup> exclusive_prefix: u32;
var<workgroup> scratch: array<u32, wg_size>;



fn calc_lookback_id(
  subgroup_invocation_id: u32,  // Now passed as an argument
  subgroup_size: u32,           // Now passed as an argument
  part_id: i32, 
  lookback_amt: i32
) -> i32 {
  
  if (lookback_amt > part_id) {
    if (subgroup_invocation_id == subgroup_size - 1) {
      return 0;
    }
    return -1;
  } else {
    return part_id - lookback_amt;
  }
}



@compute @workgroup_size(wg_size) fn prefix_sum(
        @builtin(subgroup_invocation_id) subgroup_invocation_id: u32,
        @builtin(global_invocation_id) global_id: vec3<u32>, 
        @builtin(subgroup_size) subgroup_size: u32, 
        @builtin(local_invocation_id) local_id: vec3<u32>) {

  //acquire partition index,
  if(local_id.x == 0u){
      wg_broadcast = atomicAdd(&part, 1);
  }

  let part_id = workgroupUniformLoad(&wg_broadcast);

  let sid = local_id.x / subgroup_size;  //Caution 1D workgoup ONLY! Ok, but technically not in HLSL spec
  let my_id = part_id * wg_size * BATCH_SIZE + local_id.x * BATCH_SIZE;

  var values: array<u32, BATCH_SIZE>;
  var sum = in[my_id];
  values[0] = sum;
  for (var i: u32 = 1; i < BATCH_SIZE; i++) {
      sum += in[my_id + i];
      values[i] = sum;
  }

  // Store inclusive thread prefix to local memory so that a block-wide prefix can be computed
  scratch[local_id.x] = sum;
  workgroupBarrier();

  // Perform raking exclusive sum, where only threads in the first subgroup do any work
  if (sid == 0) {
      // Each thread rakes across a block of the local prefixes
      let rake_batch_size = wg_size / subgroup_size;
      let start = local_id.x * rake_batch_size;
      for (var i = start + 1; i < start + rake_batch_size; i++) {
          scratch[i] += scratch[i - 1];
      }
      let partial_sum = scratch[start + rake_batch_size - 1];
      let prefix = subgroupExclusiveAdd(partial_sum);
      for (var i = start; i < start + rake_batch_size; i++) {
          scratch[i] += prefix;
      }
  }
  
  workgroupBarrier();

  // one thread in each block updates the aggregate/flag
  if (local_id.x == 0) { // This has to be this rather than get_local_id == 0 bcz exprfx mst be synced by subbarrier in lookback
    
    atomicStore(&prefix_states[part_id], (FLG_A << ANTI_MASK) | (scratch[local_id.x - 1] & MASK_));
    
    // first block does not need to look back
    if (part_id == 0) {
      atomicStore(&prefix_states[part_id], (FLG_P << ANTI_MASK) | (scratch[local_id.x - 1] & MASK_));
    }
    // might as well initialize exclusive prefix here too
    exclusive_prefix = 0;
  }
  workgroupBarrier();


  if (part_id == 0 && local_id.x == 0) {
    debug[0] = atomicLoad(&prefix_states[part_id]) & 0x3FFFFFFF;
  }

//   if (part_id == 1 && local_id.x == 0) {
//     debug[1] = atomicLoad(&prefix_states[0]) >> ANTI_MASK;
//   }

  if (part_id != 0 && local_id.x == 0) {
    var lookback_id = part_id - 1;
    // spin and lookback until full prefix is set
    while (lookback_id >= 0) {
      let flagg = atomicLoad(&prefix_states[lookback_id]);     
      let agg = flagg & 0x3FFFFFFF;
      let flag = flagg >> ANTI_MASK;

      if (flag == FLG_P) {
        exclusive_prefix += agg;
        break;
      } else if (flag == FLG_A) {
        exclusive_prefix += agg;
        lookback_id -= 1;
      }
    }
    atomicStore(&prefix_states[part_id], (FLG_P << ANTI_MASK) | ((exclusive_prefix + scratch[wg_size - 1]) & MASK_));
  }

  workgroupBarrier();  

  var total_exclusive_prefix : u32 = exclusive_prefix;

  if (local_id.x != 0) {
    total_exclusive_prefix += scratch[local_id.x - 1];
  }

  for (var i : u32 = 0; i < BATCH_SIZE; i++) {
    out[my_id + i] = values[i] + total_exclusive_prefix; 
  }



}