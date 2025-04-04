@group(0) @binding(0) var<storage, read_write> in: array<vec4<u32>>;
@group(0) @binding(1) var<storage, read_write> prefix_states: array<atomic<u32>>;
@group(0) @binding(2) var<storage, read_write> out: array<vec4<u32>>;
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

  if(local_id.x == 0u){
      wg_broadcast = atomicAdd(&part, 1);
  }

  let part_id = workgroupUniformLoad(&wg_broadcast);

  let sid = local_id.x / subgroup_size;  //Caution 1D workgoup ONLY! Ok, but technically not in HLSL spec
  let my_id = part_id * wg_size * BATCH_SIZE + local_id.x * BATCH_SIZE;

  var values: array<vec4<u32>, BATCH_SIZE>;

  for (var i: u32 = 0; i < BATCH_SIZE; i++) {
      values[i] = in[my_id + i];
  }

  values[0].y += values[0].x;
  values[0].z += values[0].y;
  values[0].w += values[0].z;

  for (var i: u32 = 1u; i < BATCH_SIZE; i++) {
      let prev = values[i - 1u].w;

      values[i].x += prev;
      values[i].y += values[i].x;
      values[i].z += values[i].y;
      values[i].w += values[i].z;
  }

  scratch[local_id.x] = values[BATCH_SIZE - 1].w;
  workgroupBarrier();

  if (sid == 0) {
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

  if (local_id.x == 0) { // This has to be this rather than get_local_id == 0 bcz exprfx mst be synced by subbarrier in lookback


    atomicStore(&prefix_states[part_id], (FLG_A << ANTI_MASK) | (scratch[local_id.x - 1] & MASK_));

    if (part_id == 0) {
      atomicStore(&prefix_states[part_id], (FLG_P << ANTI_MASK) | (scratch[local_id.x - 1] & MASK_));
    }
    exclusive_prefix = 0;
  }
  workgroupBarrier();



  var p : u32 = debug[0];

  if (p == 1) {
    if (part_id != 0 && sid == 0) {
      var lookback_id : i32 = i32(part_id) - (i32(subgroup_size) - i32(subgroup_invocation_id));
      var done = false;
      var flag : u32 = FLG_P;
      var agg : u32 = 0;
      while(!done) {
        if (lookback_id >= 0) {
          let flagg = atomicLoad(&prefix_states[lookback_id]);  
          agg = flagg & 0x3FFFFFFF;
          flag = flagg >> ANTI_MASK;
        }
        if (subgroupAll(flag == FLG_A || flag == FLG_P)) {
          var local_prefix = 0u;
          if (subgroupAny(flag == FLG_P)) {
            // we will terminate after this iteration
            done = true;
            let inclusive = select(0, subgroup_invocation_id, flag == FLG_P);
            let max_inclusive = subgroupMax(inclusive);

            // load thread with highest FLG_P and higher prefixes
            if (max_inclusive <= subgroup_invocation_id) {
              local_prefix = agg;
            }

          // if no thread has inclusive prefix, all threads load exclusive prefix
          } else {
            // every thread looks back another partition
            local_prefix = agg;
            lookback_id = lookback_id - i32(subgroup_size);
          }
          let scanned_prefix = subgroupInclusiveAdd(local_prefix);

          // last thread has the full prefix, update the workgroup level exclusive prefix
          if (subgroup_invocation_id == subgroup_size - 1) {
            exclusive_prefix += scanned_prefix;
          }
        }
      }
      // finally last thread in subgroup updates this workgroup's prefix/flag
      if (subgroup_invocation_id == subgroup_size - 1) {
        //debug[0] = i32(sid);
        atomicStore(&prefix_states[part_id], (FLG_P << ANTI_MASK) | ((exclusive_prefix + scratch[wg_size - 1]) & MASK_));
      }
      
    }
  }else{
    if (part_id != 0 && local_id.x == 0) {
      var lookback_id = part_id - 1;
      // spin and lookback until full prefix is set
      while (lookback_id >= 0) {
        let flagg = atomicLoad(&prefix_states[lookback_id]);     
        let agg = flagg & 0x3FFFFFFF;
        let flag = flagg >> ANTI_MASK;

        if (flag == FLG_P) {
                if (part_id == 6) {
                  debug[1] = agg;
                }
          exclusive_prefix += agg;
          break;
        } else if (flag == FLG_A) {
          exclusive_prefix += agg;
          lookback_id -= 1;
        }
      }
      atomicStore(&prefix_states[part_id], (FLG_P << ANTI_MASK) | ((exclusive_prefix + scratch[wg_size - 1]) & MASK_));
    }
  }

  workgroupBarrier();  

  var total_exclusive_prefix : u32 = exclusive_prefix;

  if (local_id.x != 0) {
    total_exclusive_prefix += scratch[local_id.x - 1];
  }

  for (var i : u32 = 0; i < BATCH_SIZE; i++) {
    out[my_id + i] = values[i] + total_exclusive_prefix; 
  }


              //   if (part_id == 2) {
              //   debug[1] = 2;
              // }

}