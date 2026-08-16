/* Copyright Vital Audio, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include "shape_batcher.h"
#include "visage_utils/space.h"

#include <unordered_map>

namespace visage {
  class Palette;
  class Shader;

  class Region {
  public:
    friend class Canvas;

    Region() = default;

    SubmitBatch* submitBatchAtPosition(int position) const {
      return shape_batcher_.batchAtIndex(position);
    }
    int numSubmitBatches() const { return shape_batcher_.numBatches(); }
    bool isEmpty() const { return shape_batcher_.isEmpty(); }
    const std::vector<Region*>& subRegions() const { return sub_regions_; }
    int numRegions() const { return sub_regions_.size(); }

    void addRegion(Region* region) {
      VISAGE_ASSERT(region->parent_ == nullptr);
      sub_regions_.push_back(region);
      region->parent_ = this;

      if (canvas_)
        region->setCanvas(canvas_);

      region->setLayerIndex(layer_index_);
    }

    void removeRegion(Region* region);
    void setCanvas(Canvas* canvas);

    void setBounds(int x, int y, int width, int height) {
      invalidate();
      x_ = x;
      y_ = y;
      width_ = width;
      height_ = height;
      setupIntermediateRegion();
      invalidate();
    }

    void setVisible(bool visible) {
      visible_ = visible;
      invalidate();
    }
    bool isVisible() const { return visible_; }
    void setOnTop(bool on_top) {
      on_top_ = on_top;
      invalidate();
    }
    bool isOnTop() const { return on_top_; }
    bool overlaps(const Region* other) const {
      return x_ < other->x_ + other->width_ && x_ + width_ > other->x_ &&
             y_ < other->y_ + other->height_ && y_ + height_ > other->y_;
    }

    int x() const { return x_; }
    int y() const { return y_; }
    int width() const { return width_; }
    int height() const { return height_; }
    float dpiScale() const;

    int backdropCount() const { return backdrop_count_; }
    int backdropCountChildren() const { return backdrop_count_children_; }
    int computeBackdropCount(int current = 0);
    bool shouldDraw(int backdrop_count) {
      if (backdrop_count_ == backdrop_count)
        return true;
      if (intermediate_region_)
        return backdrop_count_ < backdrop_count && backdrop_count_children_ >= backdrop_count;
      return false;
    }

    void invalidateRect(IBounds rect);

    void invalidate() {
      if (width_ > 0 && height_ > 0)
        invalidateRect({ 0, 0, width_, height_ });
    }

    Layer* layer() const;
    void clear();

    void setupIntermediateRegion();
    void setNeedsLayer(bool needs_layer);

    void setPostEffect(PostEffect* post_effect) {
      post_effect_ = post_effect;
      setupIntermediateRegion();
    }
    PostEffect* postEffect() const { return post_effect_; }

    void setBackdropEffect(PostEffect* backdrop_effect);
    PostEffect* backdropEffect() const { return backdrop_effect_; }

    bool needsLayer() const { return intermediate_region_.get(); }
    Region* intermediateRegion() const { return intermediate_region_.get(); }

    const PackedBrush* addBrush(GradientAtlas* atlas, const Brush& brush) {
      return addBrush(atlas, brush.gradient(), brush.position());
    }

    const PackedBrush* addBrush(GradientAtlas* atlas, const Gradient& gradient,
                                const GradientPosition& position) {
      // A FLAT COLOUR IS THE SAME BRUSH EVERY TIME IT IS ASKED FOR, so ask once and keep it.
      //
      // Every shape sets a brush, and packing one allocates a PackedBrush and then looks its
      // gradient up in the atlas BY VALUE -- three std::map operations whose comparator walks the
      // colours. All of it is thrown away in the next frame's clear() and built again. Measured
      // through a plugin editor: 290 brushes a frame drawn from 113 distinct colours, costing
      // about a fifth of the frame to build and a further tenth to destroy.
      //
      // Solid is the case worth catching. It is the overwhelming majority of a UI's shapes, and it
      // is the only one whose position carries no per-shape information: scaling a Solid position
      // leaves it identical, so the colour alone identifies the brush. A gradient's position is
      // the shape's own geometry and cannot be shared.
      //
      // resolution() == 1 is the exact test for "one colour, no stops, no repeat, no reflect" --
      // anything else needs a strip in the atlas and is not this.
      //
      // The position is checked field by field rather than by its shape, because a Solid position
      // still writes point1 and point2 into the vertices and interpolating one brush towards
      // another keeps the first one's SHAPE while moving its points. Sharing a brush is only sound
      // where the position carries nothing, so that is the test.
      if (cacheSolidBrushes() && gradient.resolution() == 1 && isSharedSolidPosition(position))
        return solidBrush(atlas, gradient.colors()[0]);

      brushes_.push_back(std::make_unique<PackedBrush>(atlas, gradient, position));
      return brushes_.back().get();
    }

    Region* parent() const { return parent_; }

  private:
    // One PackedBrush per flat colour, kept across frames. The key is a hash rather than the
    // colour, so the colour is stored beside it and compared on a hit: a collision then means
    // "not cached", which is slower and still right.
    struct SolidBrush {
      Color color;
      std::unique_ptr<PackedBrush> brush;
      uint64_t used_frame = 0;
    };

    // Held for two seconds after their last use, and SWEPT NO MORE OFTEN THAN THAT. Sweeping means
    // walking the whole cache, and a UI holds far more colours than it draws in any one frame -- a
    // plugin editor measured 286 flat brushes a frame drawn from 113 colours, over a cache of 1337
    // that were each still being asked for within the two seconds. Sweeping that every frame cost
    // more than the packing it saved, and moved the frame's time from the controls into clear()
    // rather than removing it.
    //
    // TWO FRAMES IS THE FLOOR for the lifetime, whatever these are set to: brushes_ is kept for a
    // frame after it is replaced (old_brushes_), so a brush retired the moment it stopped being
    // asked for could still be referenced by the frame that has just been submitted.
    static constexpr uint64_t kSolidBrushFrames = 120;

    // Only to stop something pathological -- a UI that never repeats a colour -- from growing the
    // cache without bound between sweeps.
    static constexpr size_t kMaxSolidBrushes = 8192;

    // The position Brush::solid() and Brush::none() produce, and the only one a shared brush can
    // stand for: everything a position can carry is absent, so two shapes asking for the same
    // colour are asking for the same thing.
    static bool isSharedSolidPosition(const GradientPosition& p) {
      return p.shape == GradientPosition::InterpolationShape::Solid && p.point1.x == 0.0f &&
             p.point1.y == 0.0f && p.point2.x == 0.0f && p.point2.y == 0.0f &&
             p.focal_radius == 0.0f && p.coefficientx2 == 0.0f && p.coefficienty2 == 0.0f &&
             p.coefficientxy == 0.0f;
    }

  public:
    // Whether the cache is doing what it was added for, which is not something to assume: a UI that
    // eases a colour produces a new one every frame, and a cache that never hits is a slower way to
    // pack a brush. Plain counters, so they can stay in.
    struct SolidStats {
      int hits = 0;
      int misses = 0;
      int collisions = 0;
      int swept = 0;
      size_t size = 0;

      void reset() { hits = misses = collisions = swept = 0; }
    };

    static SolidStats& solidStats() {
      static SolidStats stats;
      return stats;
    }

    // Off puts every brush back through the packing path, so the cache can be ruled in or out of a
    // wrong colour without a rebuild -- and so the two can be photographed against each other.
    static bool& cacheSolidBrushes() {
      static bool cache = true;
      return cache;
    }

  private:
    const PackedBrush* solidBrush(GradientAtlas* atlas, const Color& color) {
      const uint64_t key = color.toABGR16() * 0x9e3779b97f4a7c15ull +
                           static_cast<uint64_t>(color.hdr() * 65536.0f);

      auto it = solid_brushes_.find(key);
      if (it != solid_brushes_.end()) {
        if (Color::compare(it->second.color, color) == 0) {
          it->second.used_frame = brush_frame_;
          ++solidStats().hits;
          return it->second.brush.get();
        }

        ++solidStats().collisions;

        // Two different colours, one hash. Pack it the long way and do not cache it, rather than
        // evicting the incumbent and trading places with it every frame.
        brushes_.push_back(std::make_unique<PackedBrush>(atlas, Gradient(color),
                                                        GradientPosition(GradientPosition::InterpolationShape::Solid)));
        return brushes_.back().get();
      }

      ++solidStats().misses;

      SolidBrush entry;
      entry.color = color;
      entry.brush = std::make_unique<PackedBrush>(atlas, Gradient(color),
                                                  GradientPosition(GradientPosition::InterpolationShape::Solid));
      entry.used_frame = brush_frame_;
      return solid_brushes_.emplace(key, std::move(entry)).first->second.brush.get();
    }

    void sweepSolidBrushes() {
      solidStats().size = solid_brushes_.size();

      if (brush_frame_ - swept_frame_ < kSolidBrushFrames &&
          solid_brushes_.size() <= kMaxSolidBrushes)
        return;

      swept_frame_ = brush_frame_;

      for (auto it = solid_brushes_.begin(); it != solid_brushes_.end();) {
        if (brush_frame_ - it->second.used_frame > kSolidBrushFrames) {
          it = solid_brushes_.erase(it);
          ++solidStats().swept;
        }
        else
          ++it;
      }
    }

    void setLayerIndex(int layer_index);
    void incrementLayer() { setLayerIndex(layer_index_ + 1); }
    void decrementLayer() { setLayerIndex(layer_index_ - 1); }

    Text* addText(const String& string, const Font& font, Font::Justification justification) {
      text_store_.push_back(std::make_unique<Text>(string, font, justification));
      return text_store_.back().get();
    }

    void clearSubRegions() { sub_regions_.clear(); }

    void clearAll() {
      clear();
      clearSubRegions();
    }

    int x_ = 0;
    int y_ = 0;
    int width_ = 0;
    int height_ = 0;
    int palette_override_ = 0;
    bool visible_ = true;
    bool on_top_ = false;
    int layer_index_ = 0;
    int backdrop_count_ = 0;
    int backdrop_count_children_ = 0;

    Canvas* canvas_ = nullptr;
    Region* parent_ = nullptr;
    PostEffect* post_effect_ = nullptr;
    PostEffect* backdrop_effect_ = nullptr;
    ShapeBatcher shape_batcher_;
    std::vector<std::unique_ptr<PackedBrush>> brushes_;
    std::vector<std::unique_ptr<PackedBrush>> old_brushes_;
    std::unordered_map<uint64_t, SolidBrush> solid_brushes_;
    uint64_t brush_frame_ = 0;
    uint64_t swept_frame_ = 0;
    std::vector<std::unique_ptr<Text>> text_store_;
    std::vector<Region*> sub_regions_;
    std::unique_ptr<Region> intermediate_region_;
  };
}