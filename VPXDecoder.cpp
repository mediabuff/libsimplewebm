/*
	MIT License

	Copyright (c) 2016 Błażej Szczygieł

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.
*/

#include "VPXDecoder.hpp"

#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>

#include <stdlib.h>
#include <string.h>

VPXDecoder::VPXDecoder(const WebMDemuxer &demuxer, unsigned threads) :
	m_ctx(NULL),
	m_iter(NULL)
{
	const vpx_codec_dec_cfg_t codecCfg = {
		threads,
		0,
		0
	};

	vpx_codec_iface_t *codecIface = NULL;

	switch (demuxer.getVideoCodec())
	{
		case WebMDemuxer::VIDEO_VP8:
			codecIface = vpx_codec_vp8_dx();
			break;
		case WebMDemuxer::VIDEO_VP9:
			codecIface = vpx_codec_vp9_dx();
			break;
		default:
			return;
	}

	m_ctx = new vpx_codec_ctx_t;
	if (vpx_codec_dec_init(m_ctx, codecIface, &codecCfg, VPX_CODEC_USE_FRAME_THREADING))
	{
		delete m_ctx;
		m_ctx = NULL;
	}
}
VPXDecoder::~VPXDecoder()
{
	if (m_ctx)
	{
		for (std::set<double *>::iterator it = m_timesPool.begin(), itEnd = m_timesPool.end(); it != itEnd; ++it)
			delete *it;
		for (std::set<double *>::iterator it = m_timesInUse.begin(), itEnd = m_timesInUse.end(); it != itEnd; ++it)
			delete *it;
		vpx_codec_destroy(m_ctx);
		delete m_ctx;
	}
}

bool VPXDecoder::decode(const WebMDemuxer::Frame &frame)
{
	double *time;
	if (!m_timesPool.empty())
		time = *m_timesPool.begin();
	else
		time = new double;
	m_timesInUse.insert(time);
	*time = frame.time;

	m_iter = NULL;

	return !vpx_codec_decode(m_ctx, frame.buffer, frame.bufferSize, time, 0);
}
VPXDecoder::IMAGE_ERROR VPXDecoder::getImage(Image &image)
{
	IMAGE_ERROR err = NO_FRAME;
	if (vpx_image_t *img = vpx_codec_get_frame(m_ctx, &m_iter))
	{
		double *time = (double *)img->user_priv;
		if ((img->fmt & VPX_IMG_FMT_PLANAR) && !(img->fmt & (VPX_IMG_FMT_HAS_ALPHA | VPX_IMG_FMT_HIGHBITDEPTH)))
		{
			if (img->stride[0] && img->stride[1] && img->stride[2])
			{
				const int uPlane = !!(img->fmt & VPX_IMG_FMT_UV_FLIP) + 1;
				const int vPlane =  !(img->fmt & VPX_IMG_FMT_UV_FLIP) + 1;

				image.w = img->d_w;
				image.h = img->d_h;
				image.chromaShiftW = img->x_chroma_shift;
				image.chromaShiftH = img->y_chroma_shift;

				image.planes[0] = img->planes[0];
				image.planes[1] = img->planes[uPlane];
				image.planes[2] = img->planes[vPlane];

				image.linesize[0] = img->stride[0];
				image.linesize[1] = img->stride[uPlane];
				image.linesize[2] = img->stride[vPlane];

				image.time = *time;

				err = NO_ERROR;
			}
		}
		else
		{
			err = UNSUPPORTED_FRAME;
		}
		m_timesPool.insert(time);
		m_timesInUse.erase(time);
	}
	return err;
}

/**/

static inline int ceilRshift(int val, int shift)
{
	return (val + (1 << shift) - 1) >> shift;
}

int VPXDecoder::Image::getWidth(int plane) const
{
	if (!plane)
		return w;
	return ceilRshift(w, chromaShiftW);
}
int VPXDecoder::Image::getHeight(int plane) const
{
	if (!plane)
		return h;
	return ceilRshift(h, chromaShiftH);
}