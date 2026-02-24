#ifndef RESONANCE_SOFA_ASSET_H
#define RESONANCE_SOFA_ASSET_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/classes/global_constants.hpp>

namespace godot {

    /// HRTF SOFA file as Resource with volume and normalization.
    /// Use with ResonanceRuntimeConfig.hrtf_sofa_asset for custom HRTFs with gain/norm control.
    class ResonanceSOFAAsset : public Resource {
        GDCLASS(ResonanceSOFAAsset, Resource)

    public:
        enum NormType {
            NORM_NONE = 0,  /// IPL_HRTFNORMTYPE_NONE
            NORM_RMS = 1    /// IPL_HRTFNORMTYPE_RMS
        };

    private:
        PackedByteArray sofa_data;
        float volume_db = 0.0f;   /// Volume correction in dB
        int norm_type = NORM_NONE;

    protected:
        static void _bind_methods();

    public:
        ResonanceSOFAAsset();
        ~ResonanceSOFAAsset();

        void set_sofa_data(const PackedByteArray& p_data);
        PackedByteArray get_sofa_data() const;

        void set_volume_db(float p_db);
        float get_volume_db() const { return volume_db; }

        void set_norm_type(int p_type);
        int get_norm_type() const { return norm_type; }

        const uint8_t* get_data_ptr() const;
        int64_t get_size() const;

        bool is_valid() const { return sofa_data.size() > 0; }

        /// Load .sofa file from path. Returns OK on success.
        Error load_from_file(const String& p_path);

        /// Convert dB to linear gain for Phonon.
        static float db_to_gain(float db);
    };

} // namespace godot

#endif
