import argparse
import json
import os
import sys

import bpy
import bmesh


def parse_args():
    argv = sys.argv
    argv = argv[argv.index("--") + 1 :] if "--" in argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    return parser.parse_args(argv)


def connected_components(bm):
    unseen = set(bm.verts)
    components = 0
    while unseen:
        components += 1
        stack = [unseen.pop()]
        while stack:
            vertex = stack.pop()
            for edge in vertex.link_edges:
                neighbor = edge.other_vert(vertex)
                if neighbor in unseen:
                    unseen.remove(neighbor)
                    stack.append(neighbor)
    return components


def analyze_object(obj):
    mesh = obj.data
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.verts.ensure_lookup_table()
    bm.edges.ensure_lookup_table()
    bm.faces.ensure_lookup_table()

    boundary = sum(1 for edge in bm.edges if edge.is_boundary)
    non_manifold = sum(1 for edge in bm.edges if not edge.is_manifold)
    loose = sum(1 for vertex in bm.verts if not vertex.link_edges)
    components = connected_components(bm)
    volume = None
    if boundary == 0 and non_manifold == 0 and bm.faces:
        try:
            volume = abs(bm.calc_volume(signed=True))
        except Exception:
            pass

    result = {
        "object": obj.name,
        "vertices": len(bm.verts),
        "edges": len(bm.edges),
        "triangles": sum(max(len(face.verts) - 2, 0) for face in bm.faces),
        "boundary_edges": boundary,
        "non_manifold_edges": non_manifold,
        "loose_vertices": loose,
        "connected_components": components,
        "watertight_manifold": boundary == 0 and non_manifold == 0,
        "closed_volume": volume,
    }
    bm.free()
    return result


args = parse_args()
report = {"files": [], "limitations": ["self_intersection_not_tested"]}

for filename in sorted(os.listdir(args.input)):
    if not filename.lower().endswith(".fbx"):
        continue
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.fbx(filepath=os.path.join(args.input, filename))
    objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    object_reports = [analyze_object(obj) for obj in objects]
    report["files"].append(
        {
            "file": filename,
            "mesh_object_count": len(objects),
            "objects": object_reports,
            "all_objects_watertight": bool(object_reports)
            and all(item["watertight_manifold"] for item in object_reports),
        }
    )

os.makedirs(os.path.dirname(args.output), exist_ok=True)
with open(args.output, "w", encoding="utf-8") as handle:
    json.dump(report, handle, ensure_ascii=False, indent=2)
