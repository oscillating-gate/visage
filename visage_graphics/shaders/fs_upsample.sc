$input v_coordinates

// Upsample for the bloom pyramid: a nine-tap tent, the other half of the answer to the halo
// breathing as its source moves (see fs_downsample.sc for the account).
//
// A bilinear magnify reconstructs a coarse level as bilinear patches whose seams are locked to the
// coarse grid, so the reconstruction changes shape as the source crosses that grid even when the
// coarse level itself is smooth. A 3x3 tent is what the same course notes pair the thirteen-tap
// downsample with, and it costs eight more taps at the small resolutions where taps are cheapest.
//
// u_mult is kept: the caller uses it for the per-level gain that makes the levels add up to the
// intensity it wants, and the tent is normalised (16ths) so it does not change that.

#include <shader_include.sh>

uniform vec4 u_pixel_size;   // xy = one texel of the SOURCE (the coarser level), in uv
uniform vec4 u_mult;
SAMPLER2D(s_texture, 0);

void main() {
  vec2 t = u_pixel_size.xy;

  vec4 s = texture2D(s_texture, v_coordinates + vec2(-1.0,  1.0) * t);
  s += texture2D(s_texture, v_coordinates + vec2( 0.0,  1.0) * t) * 2.0;
  s += texture2D(s_texture, v_coordinates + vec2( 1.0,  1.0) * t);
  s += texture2D(s_texture, v_coordinates + vec2(-1.0,  0.0) * t) * 2.0;
  s += texture2D(s_texture, v_coordinates) * 4.0;
  s += texture2D(s_texture, v_coordinates + vec2( 1.0,  0.0) * t) * 2.0;
  s += texture2D(s_texture, v_coordinates + vec2(-1.0, -1.0) * t);
  s += texture2D(s_texture, v_coordinates + vec2( 0.0, -1.0) * t) * 2.0;
  s += texture2D(s_texture, v_coordinates + vec2( 1.0, -1.0) * t);

  gl_FragColor = s * 0.0625 * u_mult;
}
