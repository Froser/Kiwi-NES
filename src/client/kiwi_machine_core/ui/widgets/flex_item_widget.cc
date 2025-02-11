// Copyright (C) 2024 Yisi Yu
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "ui/widgets/flex_item_widget.h"

#include <SDL_image.h>
#include <imgui.h>

#include "ui/main_window.h"
#include "ui/styles.h"
#include "ui/widgets/flex_items_widget.h"
#include "utility/algorithm.h"

namespace {
constexpr float kCoverHWRatio = 250.f / 200;
constexpr float kFadeDurationInMs = 1000;
const int kBadgeSize = styles::flex_item_widget::GetBadgeSize();
constexpr int kBadgeMargin = 5;
}  // namespace

FlexItemWidget::FlexItemWidget(
    MainWindow* main_window,
    FlexItemsWidget* parent,
    std::unique_ptr<LocalizedStringUpdater> title_updater,
    TriggerCallback on_trigger)
    : Widget(main_window), main_window_(main_window), parent_(parent) {
  SDL_assert(parent_);

  // Initialize the default data
  std::unique_ptr<Data> default_data = std::make_unique<Data>();
  default_data->title_updater = std::move(title_updater);
  default_data->on_trigger_callback = std::move(on_trigger);
  current_data_ = default_data.get();
  sub_data_.push_back(std::move(default_data));

  // Since title won't change during the instance created, calculates the font
  // once to improve performance.
  SDL_assert(current_data()->title_updater);

  badge_texture_ =
      GetImage(window()->renderer(), image_resources::ImageID::kItemBadge);
}

FlexItemWidget::~FlexItemWidget() {
  for (std::unique_ptr<Data>& data : sub_data_) {
    if (data->cover_texture)
      SDL_DestroyTexture(data->cover_texture);
  }
}

void FlexItemWidget::CreateTextureIfNotExists() {
  if (!current_data()->cover_texture) {
    SDL_assert(!current_data()->cover_texture);
    SDL_RWops* bg_res = SDL_RWFromConstMem(
        const_cast<unsigned char*>(current_data()->cover_img),
        current_data()->cover_size);

    current_data()->cover_texture =
        IMG_LoadTextureTyped_RW(window()->renderer(), bg_res, 1, nullptr);
    SDL_SetTextureScaleMode(current_data()->cover_texture, SDL_ScaleModeBest);
    SDL_QueryTexture(current_data()->cover_texture, nullptr, nullptr,
                     &current_data()->cover_width,
                     &current_data()->cover_height);
  }
}

bool FlexItemWidget::MatchFilter(const std::string& filter,
                                 int& similarity) const {
  for (const auto& sub_data : sub_data_) {
    if (sub_data->title_updater->IsTitleMatchedFilter(filter, similarity))
      return true;
  }
  return false;
}

SDL_Rect FlexItemWidget::GetSuggestedSize(int item_height) {
  SDL_Rect bs = bounds();
  bs.h = item_height;
  CreateTextureIfNotExists();
  bs.w = bs.h * current_data()->cover_width / current_data()->cover_height;
  return bs;
}

void FlexItemWidget::Trigger(bool triggered_by_finger) {
  if (current_data()->on_trigger_callback)
    current_data()->on_trigger_callback.Run(triggered_by_finger);
}

void FlexItemWidget::AddSubItem(
    std::unique_ptr<LocalizedStringUpdater> title_updater,
    const kiwi::nes::Byte* cover_img_ref,
    size_t cover_size,
    TriggerCallback on_trigger) {
  std::unique_ptr<Data> data = std::make_unique<Data>();
  data->title_updater = std::move(title_updater);
  data->cover_img = cover_img_ref;
  data->cover_size = cover_size;
  data->on_trigger_callback = on_trigger;
  sub_data_.push_back(std::move(data));
}

bool FlexItemWidget::RestoreToDefaultItem() {
  bool changed = current_sub_item_index_ != 0;
  if (changed) {
    current_sub_item_index_ = 0;
    current_data_ = sub_data_[current_sub_item_index_].get();
  }
  return changed;
}

bool FlexItemWidget::SwapToNextSubItem() {
  int sub_item_index_before = current_sub_item_index_;
  ++current_sub_item_index_;
  if (current_sub_item_index_ >= sub_data_.size())
    current_sub_item_index_ = 0;

  current_data_ = sub_data_[current_sub_item_index_].get();
  return current_sub_item_index_ != sub_item_index_before;
}

void FlexItemWidget::Paint() {
  if (filtered())
    return;

  CreateTextureIfNotExists();
  const SDL_Rect kBoundsToWindow = MapToWindow(bounds());
  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  // Draw stretched image
  draw_list->AddImage(current_data()->cover_texture,
                      ImVec2(kBoundsToWindow.x, kBoundsToWindow.y),
                      ImVec2(kBoundsToWindow.x + kBoundsToWindow.w,
                             kBoundsToWindow.y + kBoundsToWindow.h));

  if (has_sub_items()) {
    // Draw a badge icon if it has sub items.
    draw_list->AddImage(
        badge_texture_,
        ImVec2(
            kBoundsToWindow.x + kBoundsToWindow.w - kBadgeMargin - kBadgeSize,
            kBoundsToWindow.y + kBadgeMargin),
        ImVec2(kBoundsToWindow.x + kBoundsToWindow.w - kBadgeMargin,
               kBoundsToWindow.y + kBadgeMargin + kBadgeSize));
  }

  // Items can be empty, because we can use filter.
  if (!parent_->empty()) {
    // Highlight selected item.
    if (parent_->IsItemSelected(this)) {
      int elapsed = fade_timer_.ElapsedInMilliseconds();
      int rgb = static_cast<int>(512 * elapsed / kFadeDurationInMs) % 512;
      if (rgb > 255)
        rgb = 512 - rgb;

      draw_list->AddRect(ImVec2(kBoundsToWindow.x, kBoundsToWindow.y),
                         ImVec2(kBoundsToWindow.x + kBoundsToWindow.w,
                                kBoundsToWindow.y + kBoundsToWindow.h),
                         ImColor(rgb, rgb, rgb));
    } else {
      fade_timer_.Reset();
    }
  }
}
