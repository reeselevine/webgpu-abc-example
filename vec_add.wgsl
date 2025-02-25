enable subgroups;

@group(0) @binding(0) var<storage, read_write> A: array<u32>;
@group(0) @binding(1) var<storage, read_write> B: array<u32>;
@group(0) @binding(2) var<storage, read_write> C: array<u32>;
@group(0) @binding(3) var<storage, read_write> part: atomic<u32>;




override wg_size: u32;
override vec_size: u32;
override bc_size: u32;

var<workgroup> wg_broadcast: u32;

//var<workgroup> scratch: array<u32, wg_size>;

@compute @workgroup_size(wg_size) fn vec_add(
    @builtin(local_invocation_id) local_id: vec3<u32>,
    @builtin(subgroup_invocation_id) lane_id: u32,
    @builtin(subgroup_size) lane_size: u32) {
  
    

    //acquire partition index,
    if(local_id.x == 0u){
        wg_broadcast = atomicAdd(&part, 1u);
    }
    let part_id = workgroupUniformLoad(&wg_broadcast);
    
    let my_id = part_id * wg_size * bc_size + local_id.x * bc_size;
    //let my_id = wg_size * bc_size + local_id.x * bc_size;

    //workgroupBarrier();
    //C[my_id] = A[my_id] + B[my_id] + 4;

    C[local_id.x] = A[local_id.x] + B[local_id.x] + 4;

}