"""在 Blender 后台把只读 Dragon FBX 转成封闭、低模的 Case 02 派生外壳。"""

import argparse
import json
import math
import os
import sys

import bpy


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--voxel-resolution", type=int, default=64)
    parser.add_argument("--target-triangles", type=int, default=6000)
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])


def mesh_stats(obj):
    mesh = obj.data
    edge_faces = {}
    signed_volume = 0.0
    for polygon in mesh.polygons:
        vertices = list(polygon.vertices)
        for index in range(len(vertices)):
            edge = tuple(sorted((vertices[index], vertices[(index + 1) % len(vertices)])))
            edge_faces[edge] = edge_faces.get(edge, 0) + 1
        if len(vertices) == 3:
            a, b, c = (mesh.vertices[index].co for index in vertices)
            signed_volume += a.dot(b.cross(c)) / 6.0
    adjacency = [set() for _ in mesh.vertices]
    for edge in mesh.edges:
        a, b = edge.vertices
        adjacency[a].add(b)
        adjacency[b].add(a)
    components = 0
    visited = set()
    for vertex in range(len(adjacency)):
        if vertex in visited:
            continue
        components += 1
        stack = [vertex]
        visited.add(vertex)
        while stack:
            current = stack.pop()
            for neighbor in adjacency[current]:
                if neighbor not in visited:
                    visited.add(neighbor)
                    stack.append(neighbor)
    world_corners = [obj.matrix_world @ mathutils_vector(corner) for corner in obj.bound_box]
    minimum = [min(corner[axis] for corner in world_corners) for axis in range(3)]
    maximum = [max(corner[axis] for corner in world_corners) for axis in range(3)]
    return {
        "vertices": len(mesh.vertices),
        "edges": len(mesh.edges),
        "triangles": sum(max(0, len(polygon.vertices) - 2) for polygon in mesh.polygons),
        "boundary_edges": sum(1 for count in edge_faces.values() if count == 1),
        "non_manifold_edges": sum(1 for count in edge_faces.values() if count != 2),
        "connected_components": components,
        "signed_volume": signed_volume,
        "bounds_min": minimum,
        "bounds_max": maximum,
        "bounds_size": [maximum[axis] - minimum[axis] for axis in range(3)],
    }


def mathutils_vector(values):
    from mathutils import Vector

    return Vector(values)


def main():
    args = parse_args()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=os.path.abspath(args.input), use_anim=False)
    mesh_objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not mesh_objects:
        raise RuntimeError("FBX 中没有 Mesh 对象")
    bpy.ops.object.select_all(action="DESELECT")
    for obj in mesh_objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = mesh_objects[0]
    bpy.ops.object.convert(target="MESH")
    if len(mesh_objects) > 1:
        bpy.ops.object.join()
    cage = bpy.context.view_layer.objects.active
    cage.name = "SM_TerrorBringer_SoftBodyCage"
    cage.parent = None
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    source_stats = mesh_stats(cage)
    maximum_extent = max(source_stats["bounds_size"])
    voxel_size = maximum_extent / max(8, args.voxel_resolution)
    cage.data.remesh_voxel_size = voxel_size
    cage.data.remesh_voxel_adaptivity = 0.0
    cage.data.use_remesh_fix_poles = True
    bpy.ops.object.voxel_remesh()
    after_voxel = mesh_stats(cage)
    if after_voxel["triangles"] > args.target_triangles:
        modifier = cage.modifiers.new("Case02_Decimate", "DECIMATE")
        modifier.ratio = args.target_triangles / after_voxel["triangles"]
        modifier.use_collapse_triangulate = True
        bpy.context.view_layer.objects.active = cage
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    triangulate = cage.modifiers.new("Case02_Triangulate", "TRIANGULATE")
    bpy.ops.object.modifier_apply(modifier=triangulate.name)
    cage.data.update()
    final_stats = mesh_stats(cage)
    if final_stats["boundary_edges"] != 0 or final_stats["non_manifold_edges"] != 0:
        raise RuntimeError(f"派生外壳不封闭：{final_stats}")
    if final_stats["connected_components"] != 1 or abs(final_stats["signed_volume"]) <= 1.0e-6:
        raise RuntimeError(f"派生外壳连通性或体积无效：{final_stats}")
    output_path = os.path.abspath(args.output)
    report_path = os.path.abspath(args.report)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    bpy.ops.wm.obj_export(
        filepath=output_path,
        export_selected_objects=True,
        export_materials=False,
        export_triangulated_mesh=True,
        forward_axis="NEGATIVE_Z",
        up_axis="Y",
    )
    report = {
        "result": "passed",
        "source": os.path.abspath(args.input),
        "output": output_path,
        "blender_version": bpy.app.version_string,
        "voxel_resolution": args.voxel_resolution,
        "voxel_size": voxel_size,
        "target_triangles": args.target_triangles,
        "source_stats": source_stats,
        "after_voxel_stats": after_voxel,
        "final_stats": final_stats,
        "license_boundary": "Derived from local Fab asset; keep local and do not redistribute without the source package license.",
    }
    with open(report_path, "w", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
    print("CASE02_DRAGON_CAGE_PASSED", json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
