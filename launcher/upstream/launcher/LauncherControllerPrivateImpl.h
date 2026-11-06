// -*- Mode: C++; indent-tabs-mode: nil; tab-width: 2 -*-
/*
 * Copyright (C) 2010-2012 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Jason Smith <jason.smith@canonical.com>
 *              Tim Penhey <tim.penhey@canonical.com>
 *              Marco Trevisan <marco.trevisan@canonical.com>
 */

namespace unity
{
namespace launcher
{

template<typename IconType>
int Controller::Impl::GetLastIconPriority(std::string const& favorite_uri, bool sticky)
{
  auto const& icons = model_->GetSublist<IconType>();
  int icon_prio = std::numeric_limits<int>::min();

  AbstractLauncherIcon::Ptr last_icon;

  // Get the last (non)-sticky icon position (if available)
  for (auto it = icons.rbegin(); it != icons.rend(); ++it)
  {
    auto const& icon = *it;
    bool update_last_icon = ((!last_icon && !sticky) || sticky);

    if (update_last_icon || icon->IsSticky() == sticky)
    {
      last_icon = icon;

      if (icon->IsSticky() == sticky)
        break;
    }
  }

  if (last_icon)
  {
    icon_prio = last_icon->SortPriority();

    if (sticky && last_icon->IsSticky() != sticky)
      icon_prio -= 1;
  }
  else if (!favorite_uri.empty())
  {
    // If we have no applications opened, we must guess it position by favorites
    for (auto const& fav : FavoriteStore::Instance().GetFavorites())
    {
      if (fav == favorite_uri)
      {
        if (icon_prio == std::numeric_limits<int>::min())
          icon_prio = (*model_->begin())->SortPriority() - 1;

        break;
      }

      auto const& icon = GetIconByUri(fav);

      if (icon)
        icon_prio = icon->SortPriority();
    }
  }

  return icon_prio;
}

} // namespace launcher
} // namespace unity
