// Regression for upstream declaration determinism. No predictive adapter is used.
#include "libslic3r/GCode/LabelObjects.hpp"
#include "libslic3r/GCode/GCodeWriter.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace Slic3r;

int main()
{
    try {
        Model model;
        for (int i = 0; i < 2; ++i) {
            auto *object = model.add_object("fixture", "", make_cube(10, 10, 2));
            object->add_instance();
            object->add_instance();
        }
        Print print;
        print.apply(model, DynamicPrintConfig::full_print_config());
        // Fixture setup only: deliberately make semantic model order oppose
        // address order, rather than hoping the allocator reproduces the defect.
        // No slicing or predictive analysis is performed in this test.
        auto &objects = const_cast<Model &>(print.model()).objects;
        std::string reference_header, reference_json;
        for (int reverse = 0; reverse < 2; ++reverse) {
            std::sort(objects.begin(), objects.end(), std::less<ModelObject *> {});
            if (reverse) std::reverse(objects.begin(), objects.end());
            if (reverse)
                for (auto *object : objects)
                    std::reverse(object->instances.begin(), object->instances.end());
            for (std::size_t i = 0; i < objects.size(); ++i)
                objects[i]->name = "semantic-" + std::to_string(i);
            GCode::LabelObjects labels;
            labels.init(print.objects(), LabelObjectsStyle::Octoprint, gcfMarlinFirmware);
            const std::string header = labels.all_objects_header();
            const std::string json = labels.all_objects_header_singleline_json();
            std::string expected = "\n";
            for (int object = 0; object < 2; ++object)
                for (int instance = 0; instance < 2; ++instance) {
                    const auto name = "semantic-" + std::to_string(object) + " id:" + std::to_string(object) +
                                      " copy " + std::to_string(instance);
                    expected += "; printing object " + name + "\n; stop printing object " + name + "\n";
                }
            expected += "\n";
            if (header != expected)
                throw std::runtime_error("analysis-disabled OctoPrint declarations depend on pointer order");
            if (reverse && (header != reference_header || json != reference_json))
                throw std::runtime_error("declarations are not repeatable under changed allocation order");
            reference_header = header;
            reference_json = json;
            GCodeWriter writer;
            writer.apply_print_config(print.config());
            writer.set_extruders({0});
            writer.set_extruder(0);
            for (const PrintObject *object : print.objects()) {
                for (const auto &instance : object->instances()) {
                    const auto *source = instance.model_instance->get_object();
                    const auto id = std::find(objects.begin(), objects.end(), source) - objects.begin();
                    const auto copy = std::find(source->instances.begin(), source->instances.end(), instance.model_instance) - source->instances.begin();
                    const auto name = source->name + " id:" + std::to_string(id) + " copy " + std::to_string(copy);
                    labels.update(&instance);
                    if (!labels.maybe_start_instance(writer).starts_with("; printing object " + name + "\n") ||
                        labels.maybe_stop_instance() != "; stop printing object " + name + "\n")
                        throw std::runtime_error("in-body object identity changed");
                }
            }
            // Firmware declarations may carry semantics. Their legacy IDs and
            // ordering must remain untouched by the OctoPrint-only fix.
            GCode::LabelObjects firmware;
            firmware.init(print.objects(), LabelObjectsStyle::Firmware, gcfMarlinFirmware);
            auto address_order = objects;
            std::sort(address_order.begin(), address_order.end(), std::less<ModelObject *> {});
            std::string firmware_expected = "\n";
            int unique_id = 0;
            for (const auto *source : address_order)
                for (const PrintObject *object : print.objects())
                    for (const auto &instance : object->instances()) {
                        if (instance.model_instance->get_object() != source) continue;
                        const auto copy = std::find(source->instances.begin(), source->instances.end(), instance.model_instance) - source->instances.begin();
                        firmware_expected += "M486 S" + std::to_string(unique_id++) + "\nM486 A" + source->name +
                                             " (Instance " + std::to_string(copy + 1) + ")\nM486 S-1\n";
                    }
            firmware_expected += "\n";
            if (firmware.all_objects_header() != firmware_expected)
                throw std::runtime_error("firmware identity or declaration ordering changed");
        }
        std::cout << "Legacy OctoPrint declarations, body identities and unchanged firmware declarations passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
