@group(0) @binding(0) var<storage, read_write> in: array<u32>;
@group(0) @binding(1) var<storage, read_write> prefix_states: array<atomic<u32>>;
@group(0) @binding(2) var<storage, read_write> out: array<u32>;
@group(0) @binding(3) var<storage, read_write> part: atomic<u32>;

const BATCH_SIZE = 4;
const FLG_A = 1u;
const FLG_P = 2u;
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



@compute @workgroup_size(wg_size) fn vec_add(
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

  // if (part_id != 0 && sid == 0) {
  //     var lookback_id = calc_lookback_id(subgroup_invocation_id, subgroup_size, i32(part_id), i32(subgroup_size - subgroup_invocation_id));
  //     var done: bool = false;
  //     // spin and lookback until full prefix is set
  //     while (!done) {
  //       var flag: u32;
  //       var agg: u32;
  //       if (lookback_id >= 0) {
  //         let flagg = atomicLoad(&prefix_states[lookback_id]);
  //         agg = flagg & 0x3FFFFFFF;
  //         flag = flagg >> ANTI_MASK; // can also just give flag as 1 if lookbackid in this thread is -1 
  //       }else{
  //         agg = 0;
  //         flag = 2;
  //       }

  //       // @TODO this is necesary
  //       // sub_group_barrier(CLK_LOCAL_MEM_FENCE);
        
  //       // check if all threads see a valid get_local_id(0) prefix
  //       if (subgroupAll(flag == 1u)) {
  //         var local_prefix: u32 = 0;
  //         // check if any thread has an inclusive prefix
  //         if (subgroupAny(flag == FLG_P)) {
  //           // we will terminate after this iteration
  //           done = true;
  //           // we want to find the highest thread with an inclusive prefix
  //           let inclusive = select(subgroup_invocation_id, 0, flag == FLG_P);
  //           // broadcast to  all threads in the subgroup the highest thread with inclusive prefix
  //           let max_inclusive = subgroupMax(inclusive);
  //           // highest thread with inclusive prefix loads it
  //           if (subgroup_invocation_id == max_inclusive) {
  //             local_prefix = select( 0u, agg, lookback_id < 0,);
  //           // threads with higher ids load exclusive prefix
  //           } else if (max_inclusive < subgroup_invocation_id) {
  //             local_prefix = agg;
  //           }
  //         // if no thread has inclusive prefix, all threads load exclusive prefix
  //         } else {
  //           // every thread looks back another partition
  //           local_prefix = agg;
  //           lookback_id = calc_lookback_id(subgroup_invocation_id, subgroup_size, lookback_id, i32(subgroup_size));
  //         }
  //         var scanned_prefix : u32 = subgroupInclusiveAdd(local_prefix);

  //         // last thread has the full prefix, update the workgroup level exclusive prefix
  //         if (subgroup_invocation_id == subgroup_size - 1) {
  //           exclusive_prefix += scanned_prefix;
  //         }
  //       }
  //     }

  //     // finally last thread in subgroup updates this workgroup's prefix/flag
  //     if (subgroup_invocation_id == subgroup_size - 1) {
  //       atomicStore(&prefix_states[part_id], (FLG_P << ANTI_MASK) | ((exclusive_prefix + scratch[wg_size - 1]) & MASK_));
  //     }
  //   }



  var total_exclusive_prefix : u32 = 0;

  if (local_id.x != 0) {
    total_exclusive_prefix += scratch[local_id.x - 1];
  }

  for (var i : u32 = 0; i < BATCH_SIZE; i++) {
    out[my_id + i] = values[i] + total_exclusive_prefix; 
  }
}