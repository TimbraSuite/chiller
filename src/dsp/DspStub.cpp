// Keeps chiller_dsp a valid library target even if the globbed DSP sources are filtered out.
namespace chiller::dsp { int dspStubAnchor() { return 0; } }
