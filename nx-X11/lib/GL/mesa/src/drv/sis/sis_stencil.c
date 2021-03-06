/**************************************************************************

Copyright 2000 Silicon Integrated Systems Corp, Inc., HsinChu, Taiwan.
All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sub license, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/
/* $XFree86: xc/lib/GL/mesa/src/drv/sis/sis_stencil.c,v 1.3 2000/09/26 15:56:49 tsi Exp $ */

/*
 * Authors:
 *    Sung-Ching Lin <sclin@sis.com.tw>
 *
 */

#include "sis_ctx.h"
#include "sis_mesa.h"

void
sis_StencilFunc (GLcontext * ctx, GLenum func, GLint ref, GLuint mask)
{
  XMesaContext xmesa = (XMesaContext) ctx->DriverCtx;
  __GLSiScontext *hwcx = (__GLSiScontext *) xmesa->private;
  __GLSiSHardware *prev = &hwcx->prev;
  __GLSiSHardware *current = &hwcx->current;

  /* set reference */ 
  current->hwStSetting = ((DWORD) ref << 8) | mask;

  current->hwStSetting &= ~0x07000000;
  switch (func)
    {
    case GL_NEVER:
      current->hwStSetting |= SiS_STENCIL_NEVER;
      break;
    case GL_LESS:
      current->hwStSetting |= SiS_STENCIL_LESS;
      break;
    case GL_EQUAL:
      current->hwStSetting |= SiS_STENCIL_EQUAL;
      break;
    case GL_LEQUAL:
      current->hwStSetting |= SiS_STENCIL_LEQUAL;
      break;
    case GL_GREATER:
      current->hwStSetting |= SiS_STENCIL_GREATER;
      break;
    case GL_NOTEQUAL:
      current->hwStSetting |= SiS_STENCIL_NOTEQUAL;
      break;
    case GL_GEQUAL:
      current->hwStSetting |= SiS_STENCIL_GEQUAL;
      break;
    case GL_ALWAYS:
      current->hwStSetting |= SiS_STENCIL_ALWAYS;
      break;
    }

  if ((current->hwStSetting2 ^ prev->hwStSetting2) ||
      (current->hwStSetting ^ prev->hwStSetting))
    {
      prev->hwStSetting = current->hwStSetting;
      prev->hwStSetting2 = current->hwStSetting2;

      hwcx->GlobalFlag |= GFLAG_STENCILSETTING;
    }
}

void
sis_StencilMask (GLcontext * ctx, GLuint mask)
{
  if (!ctx->Visual->StencilBits)
    return;

  /* set Z buffer Write Enable */
  sis_DepthMask (ctx, ctx->Depth.Mask);
}

void
sis_StencilOp (GLcontext * ctx, GLenum fail, GLenum zfail, GLenum zpass)
{
  XMesaContext xmesa = (XMesaContext) ctx->DriverCtx;
  __GLSiScontext *hwcx = (__GLSiScontext *) xmesa->private;
  __GLSiSHardware *prev = &hwcx->prev;
  __GLSiSHardware *current = &hwcx->current;

  current->hwStSetting2 &= ~0x00777000;

  switch (fail)
    {
    case GL_KEEP:
      current->hwStSetting2 |= SiS_SFAIL_KEEP;
      break;
    case GL_ZERO:
      current->hwStSetting2 |= SiS_SFAIL_ZERO;
      break;
    case GL_REPLACE:
      current->hwStSetting2 |= SiS_SFAIL_REPLACE;
      break;
    case GL_INVERT:
      current->hwStSetting2 |= SiS_SFAIL_INVERT;
      break;
    case GL_INCR:
      current->hwStSetting2 |= SiS_SFAIL_INCR;
      break;
    case GL_DECR:
      current->hwStSetting2 |= SiS_SFAIL_DECR;
      break;
    }

  switch (zfail)
    {
    case GL_KEEP:
      current->hwStSetting2 |= SiS_SPASS_ZFAIL_KEEP;
      break;
    case GL_ZERO:
      current->hwStSetting2 |= SiS_SPASS_ZFAIL_ZERO;
      break;
    case GL_REPLACE:
      current->hwStSetting2 |= SiS_SPASS_ZFAIL_REPLACE;
      break;
    case GL_INVERT:
      current->hwStSetting2 |= SiS_SPASS_ZFAIL_INVERT;
      break;
    case GL_INCR:
      current->hwStSetting2 |= SiS_SPASS_ZFAIL_INCR;
      break;
    case GL_DECR:
      current->hwStSetting2 |= SiS_SPASS_ZFAIL_DECR;
      break;
    }

  switch (zpass)
    {
    case GL_KEEP:
      current->hwStSetting2 |= SiS_SPASS_ZPASS_KEEP;
      break;
    case GL_ZERO:
      current->hwStSetting2 |= SiS_SPASS_ZPASS_ZERO;
      break;
    case GL_REPLACE:
      current->hwStSetting2 |= SiS_SPASS_ZPASS_REPLACE;
      break;
    case GL_INVERT:
      current->hwStSetting2 |= SiS_SPASS_ZPASS_INVERT;
      break;
    case GL_INCR:
      current->hwStSetting2 |= SiS_SPASS_ZPASS_INCR;
      break;
    case GL_DECR:
      current->hwStSetting2 |= SiS_SPASS_ZPASS_DECR;
      break;
    }

  if ((current->hwStSetting2 ^ prev->hwStSetting2) ||
      (current->hwStSetting ^ prev->hwStSetting))
    {
      prev->hwStSetting = current->hwStSetting;
      prev->hwStSetting2 = current->hwStSetting2;

      hwcx->GlobalFlag |= GFLAG_STENCILSETTING;

    }
}
