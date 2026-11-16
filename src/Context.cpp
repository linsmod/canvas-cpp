/**
 * Copyright (c) 2010 Daniel Wiberg
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "Context.h"

#include <cairo.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace canvas
{
  
   Context::Context(int width, int height, Canvas::Format format)
      : m_surface(NULL),
        m_cr(NULL),
        m_parser(),
        m_width(width),
        m_height(height),
        m_format(format),
        m_dirty(true)
   {
      m_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
      m_cr = cairo_create(m_surface);
      
      // Clear surface to transparent
      cairo_save(m_cr);
      cairo_set_operator(m_cr, CAIRO_OPERATOR_SOURCE);
      cairo_set_source_rgba(m_cr, 0, 0, 0, 0);
      cairo_paint(m_cr);
      cairo_restore(m_cr);
      
      // Set anti-aliasing
      cairo_set_antialias(m_cr, CAIRO_ANTIALIAS_GOOD);
      
      // Initialize with default state
      m_stateStack.push_back(State());
      syncPaintWithState();
   }
   
   Context::~Context()
   {
      if (m_cr)
         cairo_destroy(m_cr);
      if (m_surface)
         cairo_surface_destroy(m_surface);
   }

   void Context::setSize(int width, int height)
   {
      if (width == m_width && height == m_height)
         return;
      markDirty();

      cairo_surface_t * newSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
      cairo_t * newCr = cairo_create(newSurface);

      cairo_set_source_surface(newCr, m_surface, 0, 0);
      cairo_set_operator(newCr, CAIRO_OPERATOR_SOURCE);
      cairo_paint(newCr);

      cairo_destroy(m_cr);
      cairo_surface_destroy(m_surface);

      m_surface = newSurface;
      m_cr = newCr;
      m_width = width;
      m_height = height;

      cairo_set_antialias(m_cr, CAIRO_ANTIALIAS_GOOD);
      syncPaintWithState();
   }
   
   void Context::copyImageTo(void * target)
   {
      cairo_surface_flush(m_surface);
      unsigned char * source = cairo_image_surface_get_data(m_surface);
      int stride = cairo_image_surface_get_stride(m_surface);
      
      if (m_format == Canvas::kRGBA)
      {
         unsigned char * targetData = static_cast<unsigned char*>(target);
         for (int row = 0; row < m_height; row++)
         {
            for (int col = 0; col < m_width; col++)
            {
               int si = row * stride + col * 4;
               int ti = row * m_width * 4 + col * 4;
               // Cairo ARGB32 memory layout on little-endian: B, G, R, A
               // Target RGBA (as requested by kRGBA): R, G, B, A
               targetData[ti + 0] = source[si + 2]; // R
               targetData[ti + 1] = source[si + 1]; // G
               targetData[ti + 2] = source[si + 0]; // B
               targetData[ti + 3] = source[si + 3]; // A
            }
         }
      }
      else
      {
         // kARGB: same format as Cairo ARGB32 (BGRA in memory on little-endian)
         unsigned char * targetData = static_cast<unsigned char*>(target);
         for (int row = 0; row < m_height; row++)
         {
            memcpy(targetData + row * m_width * 4, source + row * stride, m_width * 4);
         }
      }
   }
   
   // =========================================================================
   // Styles
   // =========================================================================
   
   float Context::lineWidth() const
   {
      return currentState().lineWidth;
   }
   
   void Context::setLineWidth(float width)
   {
      currentState().lineWidth = width;
   }
   
   std::string Context::lineCap() const
   {
      return currentState().lineCapString;
   }
   
   void Context::setLineCap(std::string const& cap)
   {
      currentState().lineCapString = cap;
   }
   
   std::string Context::strokeStyle() const
   {
      return currentState().strokeStyleString;
   }
   
   void Context::setStrokeStyle(std::string const& style)
   {
      State & state = currentState();
      state.strokeStyleString = style;
      state.strokeStyle = m_parser.parse(style);
      state.hasStrokeGradient = false;
   }
   
   std::string Context::fillStyle() const
   {
      return currentState().fillStyleString;
   }
   
   void Context::setFillStyle(std::string const& style)
   {
      State & state = currentState();
      state.fillStyleString = style;
      state.fillStyle = m_parser.parse(style);
      state.hasFillGradient = false;
   }
   
   void Context::setFillGradient(GradientData const& grad)
   {
      State & state = currentState();
      state.hasFillGradient = true;
      state.fillGradientData = grad;
      state.fillStyleString = "gradient";
   }
   
   void Context::setStrokeGradient(GradientData const& grad)
   {
      State & state = currentState();
      state.hasStrokeGradient = true;
      state.strokeGradientData = grad;
      state.strokeStyleString = "gradient";
   }
   
   float Context::globalAlpha() const
   {
      return currentState().globalAlpha;
   }
   
   void Context::setGlobalAlpha(float alpha)
   {
      currentState().globalAlpha = alpha;
   }
   
   // =========================================================================
   // Composite operations
   // =========================================================================
   
   std::string Context::globalCompositeOperation() const
   {
      return currentState().globalCompositeOperation;
   }
   
   void Context::setGlobalCompositeOperation(std::string const& op)
   {
      currentState().globalCompositeOperation = op;
   }
   
   // =========================================================================
   // Shadow properties (stubs)
   // =========================================================================
   
   float Context::shadowBlur() const
   {
      return currentState().shadowBlur;
   }
   
   void Context::setShadowBlur(float blur)
   {
      currentState().shadowBlur = blur;
   }
   
   std::string Context::shadowColor() const
   {
      return currentState().shadowColor;
   }
   
   void Context::setShadowColor(std::string const& color)
   {
      currentState().shadowColor = color;
   }
   
   // =========================================================================
   // Image smoothing
   // =========================================================================
   
   bool Context::imageSmoothingEnabled() const
   {
      return currentState().imageSmoothingEnabled;
   }
   
   void Context::setImageSmoothingEnabled(bool enabled)
   {
      currentState().imageSmoothingEnabled = enabled;
   }
   
   std::string Context::imageSmoothingQuality() const
   {
      return currentState().imageSmoothingQuality;
   }
   
   void Context::setImageSmoothingQuality(std::string const& quality)
   {
      currentState().imageSmoothingQuality = quality;
   }
   
   // =========================================================================
   // Text properties
   // =========================================================================
   
   std::string Context::font() const
   {
      return currentState().font;
   }
   
   void Context::setFont(std::string const& fontStr)
   {
      currentState().font = fontStr;
      applyFont();
   }
   
   std::string Context::textAlign() const
   {
      return currentState().textAlign;
   }
   
   void Context::setTextAlign(std::string const& align)
   {
      currentState().textAlign = align;
   }
   
   std::string Context::textBaseline() const
   {
      return currentState().textBaseline;
   }
   
   void Context::setTextBaseline(std::string const& baseline)
   {
      currentState().textBaseline = baseline;
   }
   void Context::parseCssFont(std::string const& fontStr, std::string &family, std::string &weight, float &size)
   {
      // Default values
      family = "Noto Sans CJK SC";
      weight = "normal";
      size = 12.0f;
      
      std::string s = fontStr;
      
      // Trim leading/trailing whitespace
      size_t start = s.find_first_not_of(" \t");
      size_t end = s.find_last_not_of(" \t");
      if (start == std::string::npos) return;
      s = s.substr(start, end - start + 1);
      
      // Parse CSS font shorthand
      std::vector<std::string> tokens;
      std::istringstream iss(s);
      std::string token;
      while (iss >> token)
         tokens.push_back(token);
      
      bool foundSize = false;
      size_t sizeIndex = 0;
      
      for (size_t i = 0; i < tokens.size(); i++)
      {
         const std::string& token = tokens[i];
         
         // Check for weight
         if (token == "bold" || token == "bolder")
         {
               weight = "bold";
               continue;
         }
         
         // Check numeric weight
         if (token.length() == 3 && isdigit(token[0]))
         {
               bool allDigits = true;
               for (char c : token)
                  if (!isdigit(c)) { allDigits = false; break; }
               
               if (allDigits)
               {
                  int w = std::stoi(token);
                  if (w >= 700) weight = "bold";
                  continue;
               }
         }
         const float rootFontSize = 16.0f;
         // Check for size (handle decimals and units)
         size_t unitPos = token.find_first_of("pxptrem%");
         if (unitPos != std::string::npos) {
            std::string unit = token.substr(unitPos);
            std::string numStr = token.substr(0, unitPos);
            if (numStr.find_first_not_of("0123456789.") == std::string::npos) {
                  float value = std::stof(numStr);
                  if (unit == "px") size = value;
                  else if (unit == "pt") size = value * 96.0f / 72.0f; // pt -> px
                  else if (unit == "em") size = value * rootFontSize; // 如果有当前字体基准
                  else if (unit == "rem") size = value * rootFontSize;
                  else if (unit == "%") size = value / 100.0f * rootFontSize; // 按需
                  else size = value; // 未知单位，直接使用数值
                  foundSize = true;
                  sizeIndex = i;
                  break;
            }
         }
         
         // Skip style/variant keywords
         if (token == "italic" || token == "oblique" || token == "small-caps" ||
               token == "normal" || token == "lighter")
               continue;
      }
      
      // Extract family (everything after size)
      if (foundSize && sizeIndex + 1 < tokens.size())
      {
         std::string familyStr;
         for (size_t i = sizeIndex + 1; i < tokens.size(); i++)
         {
               if (i > sizeIndex + 1)
                  familyStr += " ";
               familyStr += tokens[i];
         }
         
         // Parse the font family list (take first valid family)
         std::string cleanFamily;
         bool inQuote = false;
         char quoteChar = 0;
         
         for (size_t i = 0; i < familyStr.length(); i++)
         {
               char c = familyStr[i];
               
               if (c == '"' || c == '\'')
               {
                  if (inQuote && c == quoteChar)
                     inQuote = false;
                  else if (!inQuote)
                  {
                     inQuote = true;
                     quoteChar = c;
                  }
                  continue;
               }
               
               if (!inQuote && c == ',')
                  break; // Stop at first font family in list
               
               cleanFamily += c;
         }
         
         // Trim
         start = cleanFamily.find_first_not_of(" \t");
         end = cleanFamily.find_last_not_of(" \t");
         if (start != std::string::npos)
               family = cleanFamily.substr(start, end - start + 1);
         else if (!cleanFamily.empty())
               family = cleanFamily;
      }
   }
//    #include <fontconfig/fontconfig.h>

// // 检查系统是否存在指定的字体族
// FcBool check_font_exists(const char *family) {
//     FcConfig *config = FcInitLoadConfigAndFonts();
//     FcPattern *pat = FcPatternCreate();
    
//     // 设置你想寻找的字体族
//     FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)family);
    
//     FcConfigSubstitute(config, pat, FcMatchPattern);
//     FcDefaultSubstitute(pat);
    
//     FcResult result;
//     // 匹配最接近的字体
//     FcPattern *match = FcFontMatch(config, pat, &result);
//     FcBool exists = FcFalse;
    
//     if (match) {
//         FcChar8 *matched_family;
//         // 获取实际匹配到的字体族名称
//         if (FcPatternGetString(match, FC_FAMILY, 0, &matched_family) == FcResultMatch) {
//             // 如果实际匹配到的名字和你想找的一致，说明系统存在该字体
//             if (strcasecmp((char *)matched_family, family) == 0) {
//                 exists = FcTrue;
//             }
//         }
//         FcPatternDestroy(match);
//     }
    
//     FcPatternDestroy(pat);
//     return exists;
// }
   void Context::applyFont()
   {
      State const& state = currentState();
      std::string family, weight;
      float size;
      
      
      parseCssFont(state.font, family, weight, size);
      printf("parseCssFont result family %s, size: %f, from requested: `%s`\n",family.c_str(),size,state.font.c_str());
  
      cairo_font_slant_t slant = CAIRO_FONT_SLANT_NORMAL;
      cairo_font_weight_t cweight = (weight == "bold") ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL;
   
      cairo_select_font_face(m_cr, family.c_str(), slant, cweight);


      int isize = (int)size;
      cairo_set_font_size(m_cr, isize);
   }
   
   // =========================================================================
   // Text methods
   // =========================================================================
   
   void Context::fillText(std::string const& text, float x, float y)
   {
      markDirty();
      applyFont();
      applyFillStyle();
      
      cairo_font_extents_t fontExtents;
      cairo_font_extents(m_cr, &fontExtents);
      
      // Calculate horizontal offset based on textAlign
      double dx = 0;
      State const& state = currentState();
      if (state.textAlign == "center" || state.textAlign == "middle")
      {
         cairo_text_extents_t textExtents;
         cairo_text_extents(m_cr, text.c_str(), &textExtents);
         dx = -textExtents.width / 2.0 - textExtents.x_bearing;
      }
      else if (state.textAlign == "right" || state.textAlign == "end")
      {
         cairo_text_extents_t textExtents;
         cairo_text_extents(m_cr, text.c_str(), &textExtents);
         dx = -textExtents.width - textExtents.x_bearing;
      }
      
      // Calculate vertical offset based on textBaseline
      double dy = 0;
      if (state.textBaseline == "top")
         dy = fontExtents.ascent;
      else if (state.textBaseline == "middle")
         dy = (fontExtents.ascent - fontExtents.descent) / 2.0;
      else if (state.textBaseline == "bottom")
         dy = -fontExtents.descent;
      // "alphabetic" is default, dy = 0
      
      cairo_move_to(m_cr, x + dx, y + dy);
      cairo_show_text(m_cr, text.c_str());
   }
   
   void Context::strokeText(std::string const& text, float x, float y)
   {
      markDirty();
      applyFont();
      applyStrokeStyle();
      
      cairo_font_extents_t fontExtents;
      cairo_font_extents(m_cr, &fontExtents);
      
      double dx = 0;
      State const& state = currentState();
      if (state.textAlign == "center" || state.textAlign == "middle")
      {
         cairo_text_extents_t textExtents;
         cairo_text_extents(m_cr, text.c_str(), &textExtents);
         dx = -textExtents.width / 2.0 - textExtents.x_bearing;
      }
      else if (state.textAlign == "right" || state.textAlign == "end")
      {
         cairo_text_extents_t textExtents;
         cairo_text_extents(m_cr, text.c_str(), &textExtents);
         dx = -textExtents.width - textExtents.x_bearing;
      }
      
      double dy = 0;
      if (state.textBaseline == "top")
         dy = fontExtents.ascent;
      else if (state.textBaseline == "middle")
         dy = (fontExtents.ascent - fontExtents.descent) / 2.0;
      else if (state.textBaseline == "bottom")
         dy = -fontExtents.descent;
      
      cairo_move_to(m_cr, x + dx, y + dy);
      cairo_text_path(m_cr, text.c_str());
      cairo_stroke(m_cr);
   }
   
   void Context::measureText(std::string const& text, double &outWidth, double &outAscent, double &outDescent)
   {
      applyFont();
      
      cairo_text_extents_t textExtents;
      cairo_text_extents(m_cr, text.c_str(), &textExtents);
      
      cairo_font_extents_t fontExtents;
      cairo_font_extents(m_cr, &fontExtents);
      
      outWidth = textExtents.x_advance > 0 ? textExtents.x_advance : textExtents.width;
      outAscent = fontExtents.ascent;
      outDescent = fontExtents.descent;
      // printf("text width measure: %f: %s\n",outWidth,text.c_str());
   }
   
   // =========================================================================
   // Transformations
   // =========================================================================
   
   void Context::scale(float x, float y)
   {
      cairo_scale(m_cr, x, y);
   }
   
   void Context::rotate(float angle)
   {
      // HTML5 Canvas rotate takes radians
      cairo_rotate(m_cr, angle);
   }
   
   void Context::translate(float x, float y)
   {
      cairo_translate(m_cr, x, y);
   }
   
   void Context::setTransform(float a, float b, float c, float d, float e, float f)
   {
      // HTML5 Canvas matrix: [a c e; b d f; 0 0 1]
      // Cairo matrix: {xx, yx, xy, yy, x0, y0}
      // xx=a, yx=b, xy=c, yy=d, x0=e, y0=f
      cairo_matrix_t matrix;
      cairo_matrix_init(&matrix, a, b, c, d, e, f);
      cairo_set_matrix(m_cr, &matrix);
   }
   
   void Context::resetTransform()
   {
      cairo_identity_matrix(m_cr);
   }
   
   // =========================================================================
   // State save/restore
   // =========================================================================
   
   void Context::save()
   {
      cairo_save(m_cr);
      // Push a copy of the current state
      m_stateStack.push_back(currentState());
   }
   
   void Context::restore()
   {
      cairo_restore(m_cr);
      // Pop the state, but keep at least one
      if (m_stateStack.size() > 1)
         m_stateStack.pop_back();
   }
   
   // =========================================================================
   // Image drawing
   // =========================================================================
   
   void Context::drawImage(ImageData * image, float x, float y, float width, float height)
   {
      markDirty();
      cairo_save(m_cr);
      
      cairo_surface_t * src = image->surface();
      int srcWidth = image->width();
      int srcHeight = image->height();
      
      // Scale source to destination size
      cairo_translate(m_cr, x, y);
      cairo_scale(m_cr, width / (double)srcWidth, height / (double)srcHeight);
      
      cairo_set_source_surface(m_cr, src, 0, 0);
      
      // Apply image smoothing filter
      State const& state = currentState();
      cairo_pattern_t * pattern = cairo_get_source(m_cr);
      if (state.imageSmoothingEnabled)
      {
         if (state.imageSmoothingQuality == "high")
            cairo_pattern_set_filter(pattern, CAIRO_FILTER_BEST);
         else if (state.imageSmoothingQuality == "medium")
            cairo_pattern_set_filter(pattern, CAIRO_FILTER_GOOD);
         else
            cairo_pattern_set_filter(pattern, CAIRO_FILTER_BILINEAR);
      }
      else
      {
         cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
      }
      
      cairo_set_operator(m_cr, CAIRO_OPERATOR_OVER);
      cairo_paint(m_cr);
      
      cairo_restore(m_cr);
   }
   
   // =========================================================================
   // Path functions
   // =========================================================================
   
   void Context::beginPath()
   {
      cairo_new_path(m_cr);
   }
   
   void Context::closePath()
   {
      cairo_close_path(m_cr);
   }

   void Context::fill()
   {
      markDirty();
      applyFillStyle();
      cairo_fill_preserve(m_cr);
   }
   
   void Context::stroke()
   {
      markDirty();
      applyStrokeStyle();
      cairo_stroke_preserve(m_cr);
   }
   
   void Context::clip()
   {
      cairo_clip_preserve(m_cr);
   }

   void Context::moveTo(float x, float y)
   {
      cairo_move_to(m_cr, x, y);
   }
   
   void Context::lineTo(float x, float y)
   {
      cairo_line_to(m_cr, x, y);
   }
   
   void Context::quadraticCurveTo(float cpx, float cpy, float x, float y)
   {
      // Cairo doesn't have quadratic curves directly, convert to cubic.
      // Get current point
      double px, py;
      cairo_get_current_point(m_cr, &px, &py);
      
      // Convert quadratic control point to two cubic control points
      double cp1x = px + 2.0 / 3.0 * (cpx - px);
      double cp1y = py + 2.0 / 3.0 * (cpy - py);
      double cp2x = x + 2.0 / 3.0 * (cpx - x);
      double cp2y = y + 2.0 / 3.0 * (cpy - y);
      
      cairo_curve_to(m_cr, cp1x, cp1y, cp2x, cp2y, x, y);
   }
   
   void Context::bezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y)
   {
      cairo_curve_to(m_cr, cp1x, cp1y, cp2x, cp2y, x, y);
   }
   
   void Context::arcTo(float x1, float y1, float x2, float y2, float radius)
   {
      // Cairo doesn't have arcTo directly. Implement using line_to + arc.
      // arcTo(x1, y1, x2, y2, radius) from HTML5 Canvas spec:
      // Draws an arc tangent to the line from current point to (x1,y1) and
      // the line from (x1,y1) to (x2,y2), with the given radius.
      
      double px, py;
      cairo_get_current_point(m_cr, &px, &py);
      
      // Vector from current point to (x1, y1)
      double dx1 = x1 - px;
      double dy1 = y1 - py;
      double len1 = sqrt(dx1 * dx1 + dy1 * dy1);
      
      // Vector from (x1, y1) to (x2, y2)
      double dx2 = x2 - x1;
      double dy2 = y2 - y1;
      double len2 = sqrt(dx2 * dx2 + dy2 * dy2);
      
      if (len1 < 1e-6 || len2 < 1e-6 || radius < 1e-6) {
         cairo_line_to(m_cr, x1, y1);
         return;
      }
      
      // Normalize direction vectors
      dx1 /= len1; dy1 /= len1;
      dx2 /= len2; dy2 /= len2;
      
      // Angle between the two lines
      double dot = dx1 * dx2 + dy1 * dy2;
      if (dot > 1.0) dot = 1.0;
      if (dot < -1.0) dot = -1.0;
      double angle = acos(dot);
      double half_angle = angle / 2.0;
      
      if (fabs(sin(half_angle)) < 1e-6) {
         // Lines are nearly parallel, just draw a line
         cairo_line_to(m_cr, x1, y1);
         return;
      }
      
      // Distance from (x1,y1) to the tangent points along each line
      double dist = radius / tan(half_angle);
      
      // Clamp so tangent point doesn't go past the current point
      if (dist > len1) dist = len1;
      
      // Tangent point on first line
      double tx1 = x1 - dx1 * dist;
      double ty1 = y1 - dy1 * dist;
      
      // Tangent point on second line
      double tx2 = x1 + dx2 * dist;
      double ty2 = y1 + dy2 * dist;
      
      // Line to first tangent point
      cairo_line_to(m_cr, tx1, ty1);
      
      // Compute arc center
      // Cross product to determine turn direction
      double cross = dx1 * dy2 - dy1 * dx2;
      
      // Perpendicular to first line at tangent point, pointing toward center
      double nx, ny;
      if (cross > 0) {
         // Left turn: center is to the right of first line direction
         nx = dy1;
         ny = -dx1;
      } else {
         // Right turn: center is to the left of first line direction
         nx = -dy1;
         ny = dx1;
      }
      
      // Center is at distance radius from tangent point, perpendicular to line
      double cx = tx1 + nx * radius;
      double cy = ty1 + ny * radius;
      
      // Compute start and end angles
      double startAngle = atan2(ty1 - cy, tx1 - cx);
      double endAngle = atan2(ty2 - cy, tx2 - cx);
      
      if (cross > 0) {
         cairo_arc(m_cr, cx, cy, radius, startAngle, endAngle);
      } else {
         cairo_arc_negative(m_cr, cx, cy, radius, startAngle, endAngle);
      }
   }
   
   void Context::rect(float x, float y, float width, float height)
   {
      cairo_rectangle(m_cr, x, y, width, height);
   }
   
   void Context::roundRect(float x, float y, float width, float height, float radius)
   {
      if (radius <= 0) {
         cairo_rectangle(m_cr, x, y, width, height);
         return;
      }
      if (radius > width / 2) radius = width / 2;
      if (radius > height / 2) radius = height / 2;

      cairo_move_to(m_cr, x + radius, y);
      cairo_line_to(m_cr, x + width - radius, y);
      cairo_arc(m_cr, x + width - radius, y + radius, radius, -M_PI_2, 0);
      cairo_line_to(m_cr, x + width, y + height - radius);
      cairo_arc(m_cr, x + width - radius, y + height - radius, radius, 0, M_PI_2);
      cairo_line_to(m_cr, x + radius, y + height);
      cairo_arc(m_cr, x + radius, y + height - radius, radius, M_PI_2, M_PI);
      cairo_line_to(m_cr, x, y + radius);
      cairo_arc(m_cr, x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
      cairo_close_path(m_cr);
   }
   
   void Context::arc(float x, float y, float radius, float startAngle, float endAngle, bool counterClockwise)
   {
      if (counterClockwise)
         cairo_arc_negative(m_cr, x, y, radius, startAngle, endAngle);
      else
         cairo_arc(m_cr, x, y, radius, startAngle, endAngle);
   }
   
   void Context::ellipse(float x, float y, float rx, float ry, float rotation, float startAngle, float endAngle, bool counterClockwise)
   {
      cairo_save(m_cr);
      cairo_translate(m_cr, x, y);
      cairo_rotate(m_cr, rotation);
      cairo_scale(m_cr, rx, ry);
      
      if (counterClockwise)
         cairo_arc_negative(m_cr, 0, 0, 1.0, startAngle, endAngle);
      else
         cairo_arc(m_cr, 0, 0, 1.0, startAngle, endAngle);
      
      cairo_restore(m_cr);
   }
   
   void Context::fillRect(float x, float y, float width, float height)
   {
      markDirty();
      // Save and restore the current path since fillRect is independent of it
      cairo_path_t * savedPath = cairo_copy_path(m_cr);
      cairo_new_path(m_cr);
      
      applyFillStyle();
      cairo_rectangle(m_cr, x, y, width, height);
      cairo_fill(m_cr);
      
      // Restore the path
      cairo_new_path(m_cr);
      cairo_append_path(m_cr, savedPath);
      cairo_path_destroy(savedPath);
   }
   
   void Context::strokeRect(float x, float y, float width, float height)
   {
      markDirty();
      // Save and restore the current path since strokeRect is independent of it
      cairo_path_t * savedPath = cairo_copy_path(m_cr);
      cairo_new_path(m_cr);
      
      applyStrokeStyle();
      cairo_rectangle(m_cr, x, y, width, height);
      cairo_stroke(m_cr);
      
      // Restore the path
      cairo_new_path(m_cr);
      cairo_append_path(m_cr, savedPath);
      cairo_path_destroy(savedPath);
   }
   
   void Context::clear()
   {
      markDirty();
      cairo_save(m_cr);
      cairo_set_operator(m_cr, CAIRO_OPERATOR_SOURCE);
      cairo_set_source_rgba(m_cr, 0, 0, 0, 0);
      cairo_paint(m_cr);
      cairo_restore(m_cr);
   }
   
   void Context::clearRect(float x, float y, float width, float height)
   {
      markDirty();
      // Save and restore the current path
      cairo_path_t * savedPath = cairo_copy_path(m_cr);
      cairo_new_path(m_cr);
      
      cairo_save(m_cr);
      cairo_set_operator(m_cr, CAIRO_OPERATOR_SOURCE);
      cairo_set_source_rgba(m_cr, 0, 0, 0, 0);
      cairo_rectangle(m_cr, x, y, width, height);
      cairo_fill(m_cr);
      cairo_restore(m_cr);
      
      // Restore the path
      cairo_new_path(m_cr);
      cairo_append_path(m_cr, savedPath);
      cairo_path_destroy(savedPath);
   }
   
   // =========================================================================
   // Internal helpers
   // =========================================================================
   
   static cairo_pattern_t * createCairoGradient(GradientData const& grad, float globalAlpha)
   {
      cairo_pattern_t * pattern = NULL;
      
      if (grad.type == GradientData::LINEAR)
      {
         pattern = cairo_pattern_create_linear(grad.x0, grad.y0, grad.x1, grad.y1);
      }
      else if (grad.type == GradientData::RADIAL)
      {
         pattern = cairo_pattern_create_radial(grad.x0, grad.y0, grad.r0, grad.x1, grad.y1, grad.r1);
      }
      
      if (pattern)
      {
         for (size_t i = 0; i < grad.stops.size(); i++)
         {
            GradientStop const& stop = grad.stops[i];
            cairo_pattern_add_color_stop_rgba(pattern, stop.offset,
               stop.color.r, stop.color.g, stop.color.b, stop.color.a * globalAlpha);
         }
      }
      
      return pattern;
   }
   
   void Context::applyCompositeOperation()
   {
      State const& state = currentState();
      std::string const& op = state.globalCompositeOperation;
      
      if (op == "source-over")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_OVER);
      else if (op == "source-atop")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_ATOP);
      else if (op == "source-in")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_IN);
      else if (op == "source-out")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_OUT);
      else if (op == "destination-over")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_DEST_OVER);
      else if (op == "destination-atop")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_DEST_ATOP);
      else if (op == "destination-in")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_DEST_IN);
      else if (op == "destination-out")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_DEST_OUT);
      else if (op == "lighter")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_ADD);
      else if (op == "copy")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_SOURCE);
      else if (op == "xor")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_XOR);
      else if (op == "multiply")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_MULTIPLY);
      else if (op == "screen")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_SCREEN);
      else if (op == "overlay")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_OVERLAY);
      else if (op == "darken")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_DARKEN);
      else if (op == "lighten")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_LIGHTEN);
      else if (op == "color-dodge")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_COLOR_DODGE);
      else if (op == "color-burn")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_COLOR_BURN);
      else if (op == "hard-light")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_HARD_LIGHT);
      else if (op == "soft-light")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_SOFT_LIGHT);
      else if (op == "difference")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_DIFFERENCE);
      else if (op == "exclusion")
         cairo_set_operator(m_cr, CAIRO_OPERATOR_EXCLUSION);
      else
         cairo_set_operator(m_cr, CAIRO_OPERATOR_OVER);
   }
   
   void Context::applyFillStyle()
   {
      State const& state = currentState();
      applyCompositeOperation();
      
      if (state.hasFillGradient)
      {
         cairo_pattern_t * pattern = createCairoGradient(state.fillGradientData, state.globalAlpha);
         if (pattern)
         {
            cairo_set_source(m_cr, pattern);
            cairo_pattern_destroy(pattern);
         }
      }
      else
      {
         state.fillStyle.applySource(m_cr, state.globalAlpha);
      }
   }
   
   void Context::applyStrokeStyle()
   {
      State const& state = currentState();
      applyCompositeOperation();
      
      if (state.hasStrokeGradient)
      {
         cairo_pattern_t * pattern = createCairoGradient(state.strokeGradientData, state.globalAlpha);
         if (pattern)
         {
            cairo_set_source(m_cr, pattern);
            cairo_pattern_destroy(pattern);
         }
      }
      else
      {
         state.strokeStyle.applySource(m_cr, state.globalAlpha);
      }
      
      cairo_set_line_width(m_cr, state.lineWidth);
      cairo_set_line_cap(m_cr, state.cairoCap());
   }
   
   void Context::syncPaintWithState()
   {
      // In Cairo, properties are set on the context right before drawing.
      // This method is kept for compatibility but the actual setting
      // happens in applyFillStyle() and applyStrokeStyle().
   }
}