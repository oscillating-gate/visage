$input v_coordinates

// Downsample for the bloom pyramid: thirteen taps arranged as five overlapping boxes.
//
// WHY NOT A PLAIN BILINEAR HALVING. The pyramid's texel grid is fixed to the region while the
// picture drawn into it moves, so a small bright source lands somewhere different inside a coarse
// texel every frame. A single bilinear tap samples that grid so sparsely that the coarse levels
// change as the source crosses it -- and since the upsample chain ADDS those levels back, the halo
// breathes: light moves between the source and the gaps around it while the total stays put. That
// reads as a very fine flicker, and it is worse the more levels there are.
//
// The five-box arrangement (Jimenez's, from the Call of Duty: Advanced Warfare course notes) covers
// the four texels being merged AND their neighbours, so the coarse level is a smooth function of
// where the source sits rather than a sample of it. Measured on CurveSeq's clock lane, where a
// scrolling train of 1px gate outlines is close to the worst case this can be given.
//
// Weights sum to 1: the centre box counts half, the four outer boxes an eighth each.

#include <shader_include.sh>

uniform vec4 u_pixel_size;   // xy = one texel of the SOURCE, in uv
SAMPLER2D(s_texture, 0);

void main() {
  vec2 t = u_pixel_size.xy;

  vec4 a = texture2D(s_texture, v_coordinates + vec2(-2.0, -2.0) * t);
  vec4 b = texture2D(s_texture, v_coordinates + vec2( 0.0, -2.0) * t);
  vec4 c = texture2D(s_texture, v_coordinates + vec2( 2.0, -2.0) * t);
  vec4 d = texture2D(s_texture, v_coordinates + vec2(-1.0, -1.0) * t);
  vec4 e = texture2D(s_texture, v_coordinates + vec2( 1.0, -1.0) * t);
  vec4 f = texture2D(s_texture, v_coordinates + vec2(-2.0,  0.0) * t);
  vec4 g = texture2D(s_texture, v_coordinates);
  vec4 h = texture2D(s_texture, v_coordinates + vec2( 2.0,  0.0) * t);
  vec4 i = texture2D(s_texture, v_coordinates + vec2(-1.0,  1.0) * t);
  vec4 j = texture2D(s_texture, v_coordinates + vec2( 1.0,  1.0) * t);
  vec4 k = texture2D(s_texture, v_coordinates + vec2(-2.0,  2.0) * t);
  vec4 l = texture2D(s_texture, v_coordinates + vec2( 0.0,  2.0) * t);
  vec4 m = texture2D(s_texture, v_coordinates + vec2( 2.0,  2.0) * t);

  gl_FragColor  = (d + e + i + j) * 0.125;      // the inner box, half the weight
  gl_FragColor += (a + b + g + f) * 0.03125;    // ...and the four that straddle its corners
  gl_FragColor += (b + c + h + g) * 0.03125;
  gl_FragColor += (f + g + l + k) * 0.03125;
  gl_FragColor += (g + h + m + l) * 0.03125;
}
