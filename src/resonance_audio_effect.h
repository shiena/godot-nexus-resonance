#ifndef RESONANCE_AUDIO_EFFECT_H
#define RESONANCE_AUDIO_EFFECT_H

#include <godot_cpp/classes/audio_effect.hpp>
#include <godot_cpp/classes/audio_effect_instance.hpp>
#include <godot_cpp/classes/ref.hpp>
#include "resonance_mixer_processor.h"

namespace godot {

    /// Resonance Reverb Bus Effect.
    /// NOTE: Reverb is applied per-source in ResonancePlayer (process_mix_direct). This bus effect
    /// reads from IPLReflectionMixer which is not populated in the current architecture. The bus
    /// is kept active for future mixer-based reverb or external sends. For now, reverb comes from
    /// each ResonancePlayer's internal pipeline.
    class ResonanceAudioEffectInstance : public AudioEffectInstance {
        GDCLASS(ResonanceAudioEffectInstance, AudioEffectInstance)

    private:
        ResonanceMixerProcessor processor;
        bool initialized_processor = false;
        int heartbeat_counter = 0;

    public:
        ResonanceAudioEffectInstance();
        ~ResonanceAudioEffectInstance();

        virtual void _process(const void* src_buffer, AudioFrame* dst_buffer, int32_t frame_count) override;
        virtual bool _process_silence() const override { return true; }  // Always process - we output mixer, not bus input

    protected:
        static void _bind_methods() {}
    };

    class ResonanceAudioEffect : public AudioEffect {
        GDCLASS(ResonanceAudioEffect, AudioEffect)

    private:
        // Future: Add Binaural Toggle, Gain, etc. here
        float gain_db = 0.0f;

    public:
        ResonanceAudioEffect();
        ~ResonanceAudioEffect();

        void set_gain_db(float p_db);
        float get_gain_db() const;

        virtual Ref<AudioEffectInstance> _instantiate() override;

    protected:
        static void _bind_methods();
    };

} // namespace godot

#endif