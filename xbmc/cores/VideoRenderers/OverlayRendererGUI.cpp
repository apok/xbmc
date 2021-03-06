/*
 *  Copyright (C) 2005-2013 Team XBMC
 *  http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "system.h"

#include "OverlayRendererGUI.h"
#include "settings/Settings.h"

#include "filesystem/File.h"
#include "Util.h"
#include "utils/URIUtils.h"
#include "utils/StringUtils.h"
#include "utils/log.h"
#include "guilib/GUITextLayout.h"
#include "guilib/GUIFontManager.h"
#include "guilib/GUIFont.h"
#include "cores/dvdplayer/DVDCodecs/Overlay/DVDOverlayText.h"
#include "cores/VideoRenderers/RenderManager.h"

using namespace OVERLAY;

static color_t color[8] = { 0xFFFFFF00
                          , 0xFFFFFFFF
                          , 0xFF0099FF
                          , 0xFF00FF00
                          , 0xFFCCFF00
                          , 0xFF00FFFF
                          , 0xFFE5E5E5
                          , 0xFFC0C0C0 };

static CGUITextLayout* GetFontLayout()
{
  if (CUtil::IsUsingTTFSubtitles())
  { std::string font_file = CSettings::Get().GetString("subtitles.font");
    std::string font_path = URIUtils::AddFileToFolder("special://home/media/Fonts/", font_file);
    if (!XFILE::CFile::Exists(font_path))
      font_path = URIUtils::AddFileToFolder("special://xbmc/media/Fonts/", font_file);

    // We scale based on PAL4x3 - this at least ensures all sizing is constant across resolutions.
    RESOLUTION_INFO pal(720, 576, 0);
    CGUIFont *subtitle_font = g_fontManager.LoadTTF("__subtitle__"
                                                    , font_path
                                                    , color[CSettings::Get().GetInt("subtitles.color")]
                                                    , 0
                                                    , CSettings::Get().GetInt("subtitles.height")
                                                    , CSettings::Get().GetInt("subtitles.style")
                                                    , false, 1.0f, 1.0f, &pal, true);
    CGUIFont *border_font   = g_fontManager.LoadTTF("__subtitleborder__"
                                                    , font_path
                                                    , 0xFF000000
                                                    , 0
                                                    , CSettings::Get().GetInt("subtitles.height")
                                                    , CSettings::Get().GetInt("subtitles.style")
                                                    , true, 1.0f, 1.0f, &pal, true);
    if (!subtitle_font || !border_font)
      CLog::Log(LOGERROR, "CGUIWindowFullScreen::OnMessage(WINDOW_INIT) - Unable to load subtitle font");
    else
      return new CGUITextLayout(subtitle_font, true, 0, border_font);
  }

  return NULL;
}

COverlayText::COverlayText(CDVDOverlayText * src)
{
  CDVDOverlayText::CElement* e = src->m_pHead;
  while (e)
  {
    if (e->IsElementType(CDVDOverlayText::ELEMENT_TYPE_TEXT))
    {
      CDVDOverlayText::CElementText* t = (CDVDOverlayText::CElementText*)e;
      m_text += t->m_text;
      m_text += "\n";
    }
    e = e->pNext;
  }

  // Avoid additional line breaks
  while(StringUtils::EndsWith(m_text, "\n"))
    m_text = StringUtils::Left(m_text, m_text.length() - 1);

  // Remove HTML-like tags from the subtitles until
  StringUtils::Replace(m_text, "\\r", "");
  StringUtils::Replace(m_text, "\r", "");
  StringUtils::Replace(m_text, "\\n", "[CR]");
  StringUtils::Replace(m_text, "\n", "[CR]");
  StringUtils::Replace(m_text, "<br>", "[CR]");
  StringUtils::Replace(m_text, "\\N", "[CR]");
  StringUtils::Replace(m_text, "<i>", "[I]");
  StringUtils::Replace(m_text, "</i>", "[/I]");
  StringUtils::Replace(m_text, "<b>", "[B]");
  StringUtils::Replace(m_text, "</b>", "[/B]");
  StringUtils::Replace(m_text, "<u>", "");
  StringUtils::Replace(m_text, "<p>", "");
  StringUtils::Replace(m_text, "<P>", "");
  StringUtils::Replace(m_text, "&nbsp;", "");
  StringUtils::Replace(m_text, "</u>", "");
  StringUtils::Replace(m_text, "</i", "[/I]"); // handle tags which aren't closed properly (happens).
  StringUtils::Replace(m_text, "</b", "[/B]");
  StringUtils::Replace(m_text, "</u", "");

  m_layout = GetFontLayout();

  m_subalign = CSettings::Get().GetInt("subtitles.align");
  if (m_subalign == SUBTITLE_ALIGN_MANUAL)
  {
    m_align  = ALIGN_SUBTITLE;
    m_pos    = POSITION_RELATIVE;
    m_x      = 0.0f;
    m_y      = 0.0f;
  }
  else
  {
    m_align  = ALIGN_VIDEO;
    m_pos    = POSITION_RELATIVE;
    m_x      = 0.5f;
    if(m_subalign == SUBTITLE_ALIGN_TOP_INSIDE
    || m_subalign == SUBTITLE_ALIGN_TOP_OUTSIDE)
      m_y    = 0.0f;
    else
      m_y    = 1.0f;
  }
  m_width  = 0;
  m_height = 0;
}

COverlayText::~COverlayText()
{
  delete m_layout;
}

void COverlayText::Render(OVERLAY::SRenderState &state)
{
  if(m_layout == NULL)
    return;

  CRect rs, rd;
  g_renderManager.GetVideoRect(rs, rd);
  RESOLUTION_INFO res = g_graphicsContext.GetResInfo();

  float width_max = (float) res.Overscan.right - res.Overscan.left;
  float y, width, height;
  m_layout->Update(m_text, width_max * 0.9f, false, true); // true to force LTR reading order (most Hebrew subs are this format)
  m_layout->GetTextExtent(width, height);

  if (m_subalign == SUBTITLE_ALIGN_MANUAL
  ||  m_subalign == SUBTITLE_ALIGN_TOP_OUTSIDE
  ||  m_subalign == SUBTITLE_ALIGN_BOTTOM_INSIDE)
    y = state.y - height;
  else
    y = state.y;

  // clamp inside screen
  y = std::max(y, (float) res.Overscan.top);
  y = std::min(y, res.Overscan.bottom - height);

  m_layout->RenderOutline(state.x, y, 0, 0xFF000000, XBFONT_CENTER_X, width_max);
}
