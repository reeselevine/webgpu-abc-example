@group(0) @binding(0) var<storage, read_write> A: array<u32>;
@group(0) @binding(1) var<storage, read_write> B: array<u32>;
@group(0) @binding(2) var<storage, read_write> C: array<u32>;
@group(0) @binding(3) var<storage, read_write> part: atomic<u32>;

override wg_size: u32;
override vec_size: u32;
override bc_size: u32;

var<workgroup> wg_broadcast: u32;

@compute @workgroup_size(wg_size) fn vec_add(@builtin(global_invocation_id) global_id: vec3<u32>, @builtin(subgroup_size) lane_size: u32, 
@builtin(local_invocation_id) local_id: vec3<u32>) {

  //acquire partition index,
  if(local_id.x == 0u){
      wg_broadcast = atomicAdd(&part, 1u);
  }
  let part_id = workgroupUniformLoad(&wg_broadcast);

  if (global_id.x < vec_size) {
    C[global_id.x] = A[global_id.x] + B[global_id.x] + lane_size + bc_size + part_id;
  }

}