"""Explicitly regenerate the small, synthetic M1 slicing inputs (not golden outputs)."""
import json
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1] / "tests/predictive/jobs"
TRIANGLES = [(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7),
             (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5),
             (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]


def amf(name, objects):
    root = ET.Element("amf", unit="millimeter", version="1.1")
    for object_index, volumes in enumerate(objects):
        obj = ET.SubElement(root, "object", id=str(object_index))
        ET.SubElement(obj, "metadata", type="name").text = f"{name}-{object_index}"
        mesh = ET.SubElement(obj, "mesh")
        vertices = ET.SubElement(mesh, "vertices")
        offset = 0
        for tool, boxes in volumes:
            volume = ET.SubElement(mesh, "volume")
            ET.SubElement(volume, "metadata", type="slic3r.extruder").text = str(tool)
            for x, y, z, w, d, h in boxes:
                points = [(x, y, z), (x+w, y, z), (x+w, y+d, z), (x, y+d, z),
                          (x, y, z+h), (x+w, y, z+h), (x+w, y+d, z+h), (x, y+d, z+h)]
                for point in points:
                    coordinates = ET.SubElement(ET.SubElement(vertices, "vertex"), "coordinates")
                    for axis, coordinate in zip("xyz", point):
                        ET.SubElement(coordinates, axis).text = str(coordinate)
                for triangle in TRIANGLES:
                    face = ET.SubElement(volume, "triangle")
                    for index, vertex in enumerate(triangle, 1):
                        ET.SubElement(face, f"v{index}").text = str(vertex + offset)
                offset += 8
    ET.indent(root)
    (ROOT / f"{name}.amf").write_bytes(ET.tostring(root, encoding="utf-8", xml_declaration=True) + b"\n")


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    cube = [(1, [(0, 0, 0, 24, 18, 6)])]
    geometry = {
        "mechanical": [[(1, [(0, 0, 0, 24, 18, 2), (0, 0, 2, 24, 3, 8)])]],
        "thin_wall": [[(1, [(0, 0, 0, 20, 0.52, 6)])]],
        "bridge_support": [[(1, [(0, 0, 0, 4, 10, 8), (20, 0, 0, 4, 10, 8), (0, 0, 8, 24, 10, 2), (0, 10, 8, 12, 8, 2)])]],
        "multi_object": [cube, [(1, [(35, 0, 0, 12, 12, 4)])]],
        "serpentine": [cube], "interlocking": [cube],
        "multi_material": [[(1, [(0, 0, 0, 12, 18, 6)]), (2, [(12, 0, 0, 12, 18, 6)])]],
    }
    overrides = {
        "mechanical": "perimeter_generator = athena\n",
        "thin_wall": "perimeter_generator = arachne\n",
        "bridge_support": "support_material = 1\nsupport_material_auto = 1\ndont_support_bridges = 0\n",
        "multi_object": "gcode_label_objects = octoprint\n",
        "serpentine": "serpentine_enabled = 1\n",
        "interlocking": "interlock_perimeters_enabled = 1\ninterlock_perimeter_count = 3\n",
        "multi_material": "nozzle_diameter = 0.4,0.4\nfilament_diameter = 1.75,1.75\nfilament_type = PLA;PETG\nsingle_extruder_multi_material = 1\nwipe_tower = 1\n",
    }
    expects = {
        "mechanical": {"generator": ["athena"]}, "thin_wall": {"generator": ["arachne"]},
        "bridge_support": {"role_bits": [8]}, "multi_object": {"object": [0, 1]},
        "serpentine": {"role_bits": [4161]}, "interlocking": {"role_bits": [2049]},
        "multi_material": {"configured_tool": [0, 1]},
    }
    (ROOT / "base.ini").write_text("""# Synthetic regression profile, not a qualified printer profile.
printer_technology = FFF
bed_shape = 0x0,220x0,220x220,0x220
max_print_height = 220
gcode_flavor = marlin2
binary_gcode = 0
layer_height = 0.2
first_layer_height = 0.2
nozzle_diameter = 0.4
filament_diameter = 1.75
filament_type = PLA
perimeter_generator = athena
perimeters = 2
top_solid_layers = 3
bottom_solid_layers = 3
fill_density = 20%
fill_pattern = rectilinear
seam_position = aligned
skirts = 1
brim_width = 2
start_gcode = G90\\nM83
end_gcode = M104 S0
""", encoding="utf-8")
    jobs = []
    for name, objects in geometry.items():
        amf(name, objects)
        (ROOT / f"{name}.ini").write_text(overrides[name], encoding="utf-8")
        jobs.append({"name": name, "models": [f"{name}.amf"], "profiles": ["base.ini", f"{name}.ini"], "expect": expects[name]})
    manifest = {"status": "unqualified_inputs_pending_dependency_complete_run", "volatile_comment_prefixes": [], "jobs": jobs}
    (ROOT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
