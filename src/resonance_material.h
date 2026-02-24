#ifndef RESONANCE_MATERIAL_H
#define RESONANCE_MATERIAL_H

#include <godot_cpp/classes/resource.hpp>
#include <phonon.h> // Steam Audio types

namespace godot {

    class ResonanceMaterial : public Resource {
        GDCLASS(ResonanceMaterial, Resource)

    private:
        // Absorption coefficients for Low (400Hz), Mid (2.5kHz), High (15kHz) bands.
        // Range: 0.0 (no absorption) to 1.0 (full absorption)
        float low_freq_absorption = 0.10f;
        float mid_freq_absorption = 0.20f;
        float high_freq_absorption = 0.30f;

        // Scattering: How much sound is reflected randomly (Roughness).
        // Range: 0.0 (mirror-like) to 1.0 (fully random)
        float scattering = 0.05f;

        // Transmission (3-band): How much sound passes through the wall per frequency band.
        // Low (up to ~800 Hz), Mid (~800 Hz - 8 kHz), High (8 kHz+)
        // Range: 0.0 (solid wall) to 1.0 (transparent)
        float transmission_low = 0.01f;
        float transmission_mid = 0.01f;
        float transmission_high = 0.01f;

    protected:
        static void _bind_methods();

    public:
        ResonanceMaterial();
        ~ResonanceMaterial();

        // Getters and Setters for Godot Inspector
        void set_low_freq_absorption(float p_val);
        float get_low_freq_absorption() const;

        void set_mid_freq_absorption(float p_val);
        float get_mid_freq_absorption() const;

        void set_high_freq_absorption(float p_val);
        float get_high_freq_absorption() const;

        void set_scattering(float p_val);
        float get_scattering() const;

        void set_transmission_low(float p_val);
        float get_transmission_low() const;
        void set_transmission_mid(float p_val);
        float get_transmission_mid() const;
        void set_transmission_high(float p_val);
        float get_transmission_high() const;

        // Deprecated: convenience setter for backward compatibility; sets all three bands.
        void set_transmission(float p_val);
        float get_transmission() const;

        // Helper: Converts this resource into a native Steam Audio struct
        IPLMaterial get_ipl_material() const;
    };

} // namespace godot

#endif // RESONANCE_MATERIAL_H