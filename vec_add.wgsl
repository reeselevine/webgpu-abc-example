@group(0) @binding(0) var<storage, read_write> A: array<u32>;
@group(0) @binding(1) var<storage, read_write> B: array<u32>;
@group(0) @binding(2) var<storage, read_write> C: array<u32>;

override wg_size: u32;
override vec_size: u32;
@compute @workgroup_size(wg_size) fn vec_add(@builtin(global_invocation_id) global_id: vec3<u32>) {

  if (global_id.x < vec_size) {
    C[global_id.x] = A[global_id.x] + B[global_id.x];
  }

}
