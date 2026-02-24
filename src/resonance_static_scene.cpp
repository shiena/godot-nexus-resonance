#include "resonance_static_scene.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

ResonanceStaticScene::ResonanceStaticScene() {}

ResonanceStaticScene::~ResonanceStaticScene() {}

void ResonanceStaticScene::set_static_scene_asset(const Ref<ResonanceGeometryAsset>& p_asset) {
    static_scene_asset = p_asset;
}

Ref<ResonanceGeometryAsset> ResonanceStaticScene::get_static_scene_asset() const {
    return static_scene_asset;
}

void ResonanceStaticScene::set_scene_name_when_exported(const String& p_name) {
    scene_name_when_exported = p_name;
}

String ResonanceStaticScene::get_scene_name_when_exported() const {
    return scene_name_when_exported;
}

bool ResonanceStaticScene::has_valid_asset() const {
    return static_scene_asset.is_valid() && static_scene_asset->is_valid();
}

void ResonanceStaticScene::set_export_hash(int64_t p_hash) {
    export_hash = p_hash;
}

int64_t ResonanceStaticScene::get_export_hash() const {
    return export_hash;
}

void ResonanceStaticScene::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_static_scene_asset", "p_asset"), &ResonanceStaticScene::set_static_scene_asset);
    ClassDB::bind_method(D_METHOD("get_static_scene_asset"), &ResonanceStaticScene::get_static_scene_asset);

    ClassDB::bind_method(D_METHOD("set_scene_name_when_exported", "p_name"), &ResonanceStaticScene::set_scene_name_when_exported);
    ClassDB::bind_method(D_METHOD("get_scene_name_when_exported"), &ResonanceStaticScene::get_scene_name_when_exported);

    ClassDB::bind_method(D_METHOD("has_valid_asset"), &ResonanceStaticScene::has_valid_asset);

    ClassDB::bind_method(D_METHOD("set_export_hash", "p_hash"), &ResonanceStaticScene::set_export_hash);
    ClassDB::bind_method(D_METHOD("get_export_hash"), &ResonanceStaticScene::get_export_hash);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "static_scene_asset", PROPERTY_HINT_RESOURCE_TYPE, "ResonanceGeometryAsset"),
        "set_static_scene_asset", "get_static_scene_asset");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "scene_name_when_exported"), "set_scene_name_when_exported", "get_scene_name_when_exported");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "export_hash", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "set_export_hash", "get_export_hash");
}
