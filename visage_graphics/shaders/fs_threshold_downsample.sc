$input v_coordinates

// The bloom's first pass: take what is above the threshold, at half resolution, WITHOUT letting
// the halving decide how much of it there is.
//
// ⚠ THIS IS WHERE A MOVING HALO BREATHES. fs_mult_threshold takes ONE bilinear tap of the
// full-resolution layer while halving it, and then applies a hard threshold to that tap. A one
// pixel bright line therefore contributes according to where the half-resolution grid happens to
// fall across it: land on it and the tap is the line's own value; land between and the tap is a
// blend, which the threshold then cuts differently. The source is on the pixel grid and steps by
// whole pixels, so as it travels it alternates between those cases several times a second, and the
// halo built from it breathes -- light moving between the source and the gaps around it while the
// total stays put. Measured on CurveSeq's clock lane: 8% of the picture, frame to frame, with the
// content otherwise identical (ClickUp 86eyutup6).
//
// ⚠ THE THRESHOLD IS APPLIED PER TAP, NOT TO THEIR AVERAGE, and that distinction is the whole
// design. Averaging first would take a 1px line at four times white down to about white and leave
// almost nothing to bleed -- the thin sources this product is made of would stop glowing. Applied
// per tap, each tap contributes the real overshoot of a real pixel and the thirteen weights merely
// decide how that is spread; the total is the same wherever the grid falls.
//
// The tap layout is the five-box arrangement fs_downsample.sc uses, for the same reason.

#include <shader_include.sh>

uniform vec4 u_threshold;
uniform vec4 u_mult;
uniform vec4 u_pixel_size;   // xy = one texel of the FULL-RESOLUTION layer, in uv

SAMPLER2D(s_texture, 0);

vec4 bleed(vec2 uv) {
  vec4 color = texture2D(s_texture, uv);
  float value = max(max(color.r, color.b), color.g) + 0.0001;
  return color * max(0.0, 1.0 - u_threshold.x / value);
}

void main() {
  vec2 t = u_pixel_size.xy;

  vec4 a = bleed(v_coordinates + vec2(-2.0, -2.0) * t);
  vec4 b = bleed(v_coordinates + vec2( 0.0, -2.0) * t);
  vec4 c = bleed(v_coordinates + vec2( 2.0, -2.0) * t);
  vec4 d = bleed(v_coordinates + vec2(-1.0, -1.0) * t);
  vec4 e = bleed(v_coordinates + vec2( 1.0, -1.0) * t);
  vec4 f = bleed(v_coordinates + vec2(-2.0,  0.0) * t);
  vec4 g = bleed(v_coordinates);
  vec4 h = bleed(v_coordinates + vec2( 2.0,  0.0) * t);
  vec4 i = bleed(v_coordinates + vec2(-1.0,  1.0) * t);
  vec4 j = bleed(v_coordinates + vec2( 1.0,  1.0) * t);
  vec4 k = bleed(v_coordinates + vec2(-2.0,  2.0) * t);
  vec4 l = bleed(v_coordinates + vec2( 0.0,  2.0) * t);
  vec4 m = bleed(v_coordinates + vec2( 2.0,  2.0) * t);

  vec4 sum  = (d + e + i + j) * 0.125;
  sum += (a + b + g + f) * 0.03125;
  sum += (b + c + h + g) * 0.03125;
  sum += (f + g + l + k) * 0.03125;
  sum += (g + h + m + l) * 0.03125;

  gl_FragColor = sum * u_mult;
  gl_FragColor.a = 1.0;
}
