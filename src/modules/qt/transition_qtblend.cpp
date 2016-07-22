/*
 * transition_qtblend.cpp -- Qt composite transition
 * Copyright (c) 2016 Jean-Baptiste Mardelle <jb@kdenlive.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "common.h"
#include <framework/mlt.h>
#include <stdio.h>
#include <QImage>
#include <QPainter>
#include <QTransform>

static int get_image( mlt_frame a_frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int writable )
{
	int error = 0;
	mlt_frame b_frame = mlt_frame_pop_frame( a_frame );
	mlt_properties b_properties = MLT_FRAME_PROPERTIES( b_frame );
	mlt_properties properties = MLT_FRAME_PROPERTIES( a_frame );
	mlt_transition transition = MLT_TRANSITION( mlt_frame_pop_service( a_frame ) );
	mlt_properties transition_properties = MLT_TRANSITION_PROPERTIES( transition );

	uint8_t *b_image = NULL;
	bool hasAlpha = false;
	double opacity = 1.0;
	int b_width = *width;
	int b_height = *height;
	QTransform transform;
	// reference rect
	mlt_rect rect;
	rect.w = -1;
	rect.h = -1;

	// Determine length
	mlt_position length = mlt_transition_get_length( transition );
	// Get current position
	mlt_position position =  mlt_transition_get_position( transition, a_frame );

	// Check transform
	if ( mlt_properties_get( transition_properties, "rect" ) )
	{
		rect = mlt_properties_anim_get_rect( transition_properties, "rect", position, length );
		transform.translate(rect.x, rect.y);
		b_width = rect.w;
		b_height = rect.h;
		opacity = rect.o;
	}

	if ( mlt_properties_get( transition_properties, "rotation" ) )
	{
		double angle = mlt_properties_anim_get_double( transition_properties, "rotation", position, length );
		transform.rotate( angle );
		hasAlpha = true;
	}

	// This is not a field-aware transform.
	mlt_properties_set_int( b_properties, "consumer_deinterlace", 1 );

	// Request full resolution of b frame image.
	mlt_properties_set_int( b_properties, "rescale_width", b_width );
	mlt_properties_set_int( b_properties, "rescale_height", b_height );

	// Suppress padding and aspect normalization.
	char *interps = mlt_properties_get( properties, "rescale.interp" );
	if ( interps )
		interps = strdup( interps );
	mlt_properties_set( b_properties, "rescale.interp", "none" );

	// fetch image
	*format = mlt_image_rgb24a;
	error = mlt_frame_get_image( b_frame, &b_image, format, &b_width, &b_height, writable );

	if (error)
	{
		return error;
	}

	if ( rect.w != -1 )
	{
		// resize to rect
		if ( mlt_properties_get_int( transition_properties, "distort" ) && b_width != 0 && b_height != 0 )
		{
			transform.scale( rect.w / b_width, rect.h / b_height );
		}
		else
		{
			double scale = qMin( rect.w / b_width, rect.h / b_height );
			transform.scale( scale, scale );
		}

		if ( opacity < 1 || transform.isScaling() || transform.isTranslating() )
		{
			// we will process operations on top frame, so also process b_frame
			hasAlpha = true;
		}
	}

	if ( !hasAlpha && ( mlt_properties_get_int( transition_properties, "compositing" ) != 0 || b_width < *width || b_height < *height ) )
	{
		hasAlpha = true;
	}

	if (!hasAlpha)
	{
		// If no transform, check if top frame has an alpha channel
		uint8_t *src = b_image;
		int y = b_height + 1;
		while ( --y )
		{
			int x = b_width + 1;
			while ( --x )
			{
				if (src[3] < 255 )
				{
					hasAlpha = true;
					break;
				}
				src += 4;
			}
		}
	}
	if (!hasAlpha)
	{
		// Prepare output image
		*width = b_width;
		*height = b_height;
		int image_size = mlt_image_format_size( *format, *width, *height, NULL );
		*image = (uint8_t *) mlt_pool_alloc( image_size );
		// No transparency, return top frame
		memcpy( *image, b_image, image_size );
		mlt_properties_set_data( properties, "image", *image, image_size, mlt_pool_release, NULL );
		return 0;
	}
	// Prepare output image
	int image_size = mlt_image_format_size( *format, *width, *height, NULL );
	*image = (uint8_t *) mlt_pool_alloc( image_size );

	// Get bottom frame
	uint8_t *a_image = NULL;
	error = mlt_frame_get_image( a_frame, &a_image, format, width, height, 1 );
	if (error)
	{
		return error;
	}

	// Copy bottom frame in output
	memcpy( *image, a_image, image_size );

	bool hqPainting = false;
	if ( interps )
	{
		if ( strcmp( interps, "bilinear" ) == 0 || strcmp( interps, "bicubic" ) == 0 )
		{
			hqPainting = true;
		}
	}

	// convert bottom mlt image to qimage
	QImage bottomImg;
	convert_mlt_to_qimage_rgba( *image, &bottomImg, *width, *height );

	// convert top mlt image to qimage
	QImage topImg;
	convert_mlt_to_qimage_rgba( b_image, &topImg, b_width, b_height );

	// setup Qt drawing
	QPainter painter( &bottomImg );
	painter.setCompositionMode( ( QPainter::CompositionMode ) mlt_properties_get_int( transition_properties, "compositing" ) );
	painter.setRenderHints( QPainter::Antialiasing | QPainter::SmoothPixmapTransform, hqPainting );
	if ( mlt_properties_get( transition_properties, "rect" ) ) {
		painter.setTransform(transform);
		painter.setOpacity(opacity);
	}

	// Composite top frame
	painter.drawImage(0, 0, topImg);

	// finish Qt drawing
	painter.end();
	convert_qimage_to_mlt_rgba( &bottomImg, *image, *width, *height );
	mlt_properties_set_data( properties, "image", *image, image_size, mlt_pool_release, NULL );

	return error;
}

static mlt_frame process( mlt_transition transition, mlt_frame a_frame, mlt_frame b_frame )
{
	mlt_frame_push_service( a_frame, transition );
	mlt_frame_push_frame( a_frame, b_frame );
	mlt_frame_push_get_image( a_frame, get_image );
	return a_frame;
}

extern "C" {

mlt_transition transition_qtblend_init( mlt_profile profile, mlt_service_type type, const char *id, void *arg )
{
	mlt_transition transition = mlt_transition_new();

	if ( transition )
	{
		mlt_properties properties = MLT_TRANSITION_PROPERTIES( transition );

		if ( !createQApplicationIfNeeded( MLT_TRANSITION_SERVICE(transition) ) )
		{
			mlt_transition_close( transition );
			return NULL;
		}
		transition->process = process;
		mlt_properties_set_int( properties, "_transition_type", 1 ); // video only
		mlt_properties_set( properties, "rect", (char *) arg );
		mlt_properties_set_int( properties, "compositing", 0 );
		mlt_properties_set_int( properties, "distort", 0 );
	}

	return transition;
}

} // extern "C"
