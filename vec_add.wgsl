
@group(0) @binding(0) var<storage, read_write> in: array<u32>;
@group(0) @binding(1) var<storage, read_write> prefix_states: array<u32>;
@group(0) @binding(2) var<storage, read_write> out: array<u32>;
@group(0) @binding(3) var<storage, read_write> part: atomic<u32>;

const BATCH_SIZE = 4;

override wg_size: u32;
override vec_size: u32;

var<workgroup> wg_broadcast: u32;
var<workgroup> scratch: array<u32, wg_size>;

@compute @workgroup_size(wg_size) fn vec_add(@builtin(global_invocation_id) global_id: vec3<u32>, @builtin(subgroup_size) subgroup_size: u32, 
@builtin(local_invocation_id) local_id: vec3<u32>) {

  //acquire partition index,
  if(local_id.x == 0u){
      wg_broadcast = atomicAdd(&part, 1u);
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

  var total_exclusive_prefix : u32 = 0;

  if (local_id.x != 0) {
    total_exclusive_prefix += scratch[local_id.x - 1];
  }

  for (var i : u32 = 0; i < BATCH_SIZE; i++) {
    out[my_id + i] = values[i] + total_exclusive_prefix + prefix_states[my_id] + vec_size - vec_size; // vec_size must be removed or used or the shader breaks
  }
}