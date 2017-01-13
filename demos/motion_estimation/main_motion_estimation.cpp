/*
# Copyright (c) 2014-2016, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <memory>

#include <NVX/nvx.h>
#include <NVX/nvxcu.h>
#include <NVX/nvx_timer.hpp>
#include <VX/vx_types.h>

#include "NVXIO/Application.hpp"
#include "NVXIO/ConfigParser.hpp"
#include "NVXIO/FrameSource.hpp"
#include "NVXIO/Render.hpp"
#include "NVXIO/SyncTimer.hpp"
#include "NVXIO/Utility.hpp"
// includes CUDA Runtime
#include <cuda_runtime.h>

#include "iterative_motion_estimator.hpp"
#include "gstCamera.h"
#include "rtpStream.h"
#include "cudaYUV.h"

#define HEIGHT 480  // Lower resolution stream for RTP
#define WIDTH 640

#define HEADLESS 1  // Dont put anything out on the local display
#define GST_MULTICAST 0
#define GST_SOURCE 1
#define GST_RTP_SINK 1

#if GST_RTP_SINK
#include "rtpStream.h"
#endif

extern void DumpHex(const void* data, size_t size);
using namespace nvxio;

class gstSource : public FrameSource
{
public:
	gstSource(ContextGuard *con);
	bool open();
    FrameSource::FrameStatus fetch(vx_image image, vx_uint32 timeout = 5 /*milliseconds*/);
    FrameSource::Parameters getConfiguration();
	bool setConfiguration(const FrameSource::Parameters& params) {};
	void* getLastGPUBuffer() { return GPUBufferRGB; };
	void close();
	static void dumpVxImage(vx_image image);
private:
    int mHeight;
	ContextGuard *context;
	gstCamera *camera;
	const FrameSource::SourceType  sourceType = VIDEO_SOURCE;
	const std::string sourceName = "gst-stream";
	char buffer[HEIGHT * WIDTH * 4];
	void *GPUBufferRGB;
 };

gstSource::gstSource(nvxio::ContextGuard *con)
{
	GPUBufferRGB = 0;
    context = con;
	std::ostringstream pipeline;
#if GST_MULTICAST
	pipeline << "udpsrc address=239.192.1.43 port=5004 caps=\"application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)RAW, sampling=(string)YCbCr-4:2:2, depth=(string)8, width=(string)" << WIDTH << ", height=(string)" << HEIGHT << ", payload=(int)96\" ! ";
    printf("udpsrc address=239.192.1.44 port=5004 \n");
#else
	pipeline << "udpsrc port=5004 caps=\"application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)RAW, sampling=(string)YCbCr-4:2:2, depth=(string)8, width=(string)" << WIDTH << ", height=(string)" << HEIGHT << ", payload=(int)96\" ! ";
    printf("udpsrc port=5004 \n");
#endif
	pipeline << "queue ! rtpvrawdepay ! queue ! ";
	pipeline << "appsink name=mysink";

	static  std::string pip = pipeline.str();
	camera = gstCamera::Create(pip, HEIGHT, WIDTH);
}

bool gstSource::open()
{
	if( !camera->Open() )
	{
		printf("\nmotion-estimation:  failed to open camera for streaming\n");
		return false;
	}

	return true;
}

void gstSource::dumpVxImage(vx_image image)
{
    vx_uint32 width = 0, height = 0;
    vx_df_image format;
    vx_size planes, size;
    vx_color_space_e space;
    vx_memory_type_e type;
    vx_channel_range_e range;
    vxQueryImage( image, VX_IMAGE_ATTRIBUTE_WIDTH,  &width,  sizeof( width ) );
    vxQueryImage( image, VX_IMAGE_ATTRIBUTE_HEIGHT, &height, sizeof( height ) );
    vxQueryImage( image, VX_IMAGE_FORMAT, &format, sizeof( format ) );
    vxQueryImage( image, VX_IMAGE_PLANES, &planes, sizeof( planes ) );
    vxQueryImage( image, VX_IMAGE_SPACE, &space, sizeof( space ) );
    vxQueryImage( image, VX_IMAGE_RANGE, &range, sizeof( range ) );
    vxQueryImage( image, VX_IMAGE_SIZE, &size, sizeof( size ) );
    vxQueryImage( image, VX_IMAGE_MEMORY_TYPE, &type, sizeof( type ) );

	printf(">>> vxQueary :\n\twidth = %d\n\theight = %d\n\tplanes= %d\n\tsize = %d\n\tformat = ", (int)width, (int)height, (int)planes, ( int)size);

	switch (format)
	{
	case VX_COLOR_SPACE_NONE :
		printf("VX_DF_IMAGE_VIRT");
		break;
	case VX_DF_IMAGE_RGB :
		printf("VX_DF_IMAGE_RGB");
		break;
	case VX_DF_IMAGE_RGBX :
		printf("VX_DF_IMAGE_RGBX");
		break;
	case VX_DF_IMAGE_NV12 :
		printf("VX_DF_IMAGE_NV12");
		break;
	case VX_DF_IMAGE_NV21 :
		printf("VX_DF_IMAGE_NV21");
		break;
	case VX_DF_IMAGE_UYVY :
		printf("VX_DF_IMAGE_UYVY");
		break;
	case VX_DF_IMAGE_YUYV :
		printf("VX_DF_IMAGE_YUYV");
		break;
	case VX_DF_IMAGE_IYUV :
		printf("VX_DF_IMAGE_IYUV");
		break;
	case VX_DF_IMAGE_YUV4 :
		printf("VX_DF_IMAGE_YUV4");
		break;
	case VX_DF_IMAGE_U8 :
		printf("VX_DF_IMAGE_U8");
		break;
	case VX_DF_IMAGE_U16 :
		printf("VX_DF_IMAGE_U16");
		break;
	case VX_DF_IMAGE_S16 :
		printf("VX_DF_IMAGE_S16");
		break;
	case VX_DF_IMAGE_U32 :
		printf("VX_DF_IMAGE_U32");
		break;
	case VX_DF_IMAGE_S32 :
		printf("VX_DF_IMAGE_S32");
		break;
	default :
		printf("???");
		break;
	}
	printf("\n\tspace = ");
	switch (space)
	{
	case VX_COLOR_SPACE_NONE :
		printf("VX_COLOR_SPACE_NONE");
		break;
	case VX_COLOR_SPACE_BT601_525 :
		printf("VX_COLOR_SPACE_BT601_525");
		break;
	case VX_COLOR_SPACE_BT601_625 :
		printf("VX_COLOR_SPACE_BT601_625 ");
		break;
	case VX_COLOR_SPACE_BT709 :
		printf("VX_COLOR_SPACE_BT709 ");
		break;
	default :
		printf("???");
		break;
	}
	printf("\n\trange = ");
	switch (range)
	{
	case VX_CHANNEL_RANGE_FULL :
		printf("VX_CHANNEL_RANGE_FULL");
		break;
	case VX_CHANNEL_RANGE_RESTRICTED :
		printf("VX_CHANNEL_RANGE_RESTRICTED");
		break;
	default :
		printf("???");
		break;
	}
	printf("\n\tmemory = ");
	switch (type)
	{
	case VX_MEMORY_TYPE_NONE  :
		printf("VX_MEMORY_TYPE_NONE ");
		break;
	case VX_MEMORY_TYPE_HOST  :
		printf("VX_MEMORY_TYPE_HOST ");
		break;
	default :
		printf("???");
		break;
	}
	printf("\n");
}

FrameSource::FrameStatus gstSource::fetch(vx_image image, vx_uint32 timeout)
{
	void *GPUbuffer = NULL;
	void *CPUbuffer = NULL;
	vx_status status;

	if( !camera->Capture(&CPUbuffer, &GPUbuffer, 1000) )
	{
		printf("\nmotion-estimation:  failed to capture frame\n");
		return CLOSED;
	}

	const vx_rectangle_t rect = { 0, 0, WIDTH, HEIGHT};
	const vx_imagepatch_addressing_t src_addr = {
        WIDTH, HEIGHT, sizeof(vx_uint8)*4, WIDTH * sizeof(vx_uint8)*4, VX_SCALE_UNITY, VX_SCALE_UNITY, 1, 1 };

	if ( camera->ConvertYUVtoRGBA(GPUbuffer, (void**)&buffer, (void**)&GPUBufferRGB) )
	{
		vxCopyImagePatch(	image,
							&rect,
							0,
							&src_addr,
							buffer,
							VX_WRITE_ONLY,
							VX_MEMORY_TYPE_HOST
		);
	}
	else
	{
		printf("\nmotion-estimation: failed CUDA YUV to RGB\n");
		return CLOSED;
	}

	status = vxGetStatus((vx_reference)image);
	if (status == VX_SUCCESS)
		return OK;
	else
	{
		printf("\nmotion-estimation: vxCopyImagePatch failed\n");
		return CLOSED;
	}
}

void gstSource::close()
{
	camera->Close();
	return;
}

FrameSource::Parameters gstSource::getConfiguration()
{
	FrameSource::Parameters param;
	param.format = NVXCU_DF_IMAGE_U8;
	param.fps = 25;
	param.frameHeight = HEIGHT;
	param.frameWidth = WIDTH;

	return param;
};

//
// Process events
//

struct EventData
{
    EventData() : stop(false), pause(false) {}

    bool stop;
    bool pause;
};


static void keyboardEventCallback(void* eventData, vx_char key, vx_uint32, vx_uint32)
{
    EventData* data = static_cast<EventData*>(eventData);

    if (key == 27) // escape
    {
        data->stop = true;
    }
    else if (key == ' ') // space
    {
        data->pause = !data->pause;
    }
}

//
// Parse configuration file
//

static bool read(const std::string& configFile,
                 IterativeMotionEstimator::Params& params,
                 std::string& message)
{
    std::unique_ptr<nvxio::ConfigParser> parser(nvxio::createConfigParser());

    parser->addParameter("biasWeight", nvxio::OptionHandler::real(&params.biasWeight,
             nvxio::ranges::atLeast(0.0f)));
    parser->addParameter("mvDivFactor", nvxio::OptionHandler::integer(&params.mvDivFactor,
             nvxio::ranges::atLeast(0) & nvxio::ranges::atMost(16)));
    parser->addParameter("smoothnessFactor", nvxio::OptionHandler::real(&params.smoothnessFactor,
             nvxio::ranges::atLeast(0.0f)));

    message = parser->parse(configFile);

    return message.empty();
}

//
// main - Application entry point
//

int main(int argc, char** argv)
{
	vx_status status;
	int deviceCount = 0;

#if GST_SOURCE
    std::cout << "Abaco Systems (ross.newman@abaco.com)\n\t Modified motion estimation for Gstreamer enabled RTP streams.\n\tOriginal demonstration code by Nvidia (see source licence included in headers).\n\n";
#endif
    try
    {
        nvxio::Application &app = nvxio::Application::get();

        //
        // Parse command line arguments
        //

        app.setDescription("This sample demonstrates Iterative Motion Estimation algorithm");
        std::string configFile = app.findSampleFilePath("motion_estimation_demo_config.ini");
        app.addOption('c', "config", "Config file path", nvxio::OptionHandler::string(&configFile));
#if !GST_SOURCE
		// Uses other video gst RTP video source
        std::string sourceUri = app.findSampleFilePath("pedestrians.mp4");
        app.addOption('s', "source", "Source URI", nvxio::OptionHandler::string(&sourceUri));
#endif
        app.init(argc, argv);

        //
        // Reads and checks input parameters
        //

        IterativeMotionEstimator::Params params;
        std::string error;
        if (!read(configFile, params, error))
        {
            std::cout << error;
            return nvxio::Application::APP_EXIT_CODE_INVALID_VALUE;
        }

        //
        // Create OpenVX context
        //

        nvxio::ContextGuard context;
        vxDirective(context, VX_DIRECTIVE_ENABLE_PERFORMANCE);

        //
        // Messages generated by the OpenVX framework will be processed by nvxio::stdoutLogCallback
        //

        vxRegisterLogCallback(context, &nvxio::stdoutLogCallback, vx_false_e);

        //
        // Create a Frame Source
        //
#if GST_SOURCE
		gstSource *frameSource = new gstSource(&context);
#else
        std::unique_ptr<nvxio::FrameSource> frameSource(nvxio::createDefaultFrameSource(context, sourceUri));
#endif
        if (!frameSource || !frameSource->open())
        {
            std::cerr << "Error: cannot open frame source!" << std::endl;
            return nvxio::Application::APP_EXIT_CODE_NO_RESOURCE;
        }

        if (frameSource->getSourceType() == nvxio::FrameSource::SINGLE_IMAGE_SOURCE)
        {
            std::cerr << "Can't work on a single image." << std::endl;
            return nvxio::Application::APP_EXIT_CODE_INVALID_FORMAT;
        }

        nvxio::FrameSource::Parameters frameConfig = frameSource->getConfiguration();

#if GST_RTP_SINK
        //
        // Initalise RTP streaming output
        //

		FrameSource::Parameters srcparams = frameSource->getConfiguration();

#if	GST_MULTICAST
		rtpStream rtpStreaming(srcparams.frameHeight, srcparams.frameWidth, (char*)"239.192.1.198", 5004);
#else
		rtpStream rtpStreaming(srcparams.frameHeight, srcparams.frameWidth, (char*)"127.0.0.1", 5005);
#endif

		rtpStreaming.Open();
#endif

#if !HEADLESS
		//
		// Create a Render
		//
		// TODO get rid of rendrer completely not needed for RTP streams.
		std::unique_ptr<nvxio::Render> render = nvxio::createDefaultRender(context, "Motion Estimation Demo",
																		   frameConfig.frameWidth, frameConfig.frameHeight);

		if (!render)
		{
			std::cerr << "Error: Cannot create render!" << std::endl;
			return nvxio::Application::APP_EXIT_CODE_NO_RENDER;
		}

		EventData eventData;
		render->setOnKeyboardEventCallback(keyboardEventCallback, &eventData);
#else
		EventData eventData;
		eventData.pause = false;
		eventData.stop =false;
#endif

        //
        // Create OpenVX Image to hold frames from video source
        //
        vx_image frameExemplar = vxCreateImage(context,
            frameConfig.frameWidth, frameConfig.frameHeight, VX_DF_IMAGE_RGBX);
        NVXIO_CHECK_REFERENCE(frameExemplar);
        vx_delay frame_delay = vxCreateDelay(context, (vx_reference)frameExemplar, 2);
        NVXIO_CHECK_REFERENCE(frame_delay);
        vxReleaseImage(&frameExemplar);

        vx_image prevFrame = (vx_image)vxGetReferenceFromDelay(frame_delay, -1);
        vx_image currFrame = (vx_image)vxGetReferenceFromDelay(frame_delay, 0);

        //
        // Create algorithm
        //

        IterativeMotionEstimator ime(context);

        nvxio::FrameSource::FrameStatus frameStatus;
        do
        {
            frameStatus = frameSource->fetch(prevFrame);
        } while (frameStatus == nvxio::FrameSource::TIMEOUT);
        if (frameStatus == nvxio::FrameSource::CLOSED)
        {
            std::cerr << "Source has no frames" << std::endl;
            return nvxio::Application::APP_EXIT_CODE_NO_FRAMESOURCE;
        }

        ime.init(prevFrame, currFrame, params);

        //
        // Main loop
        //

        std::unique_ptr<nvxio::SyncTimer> syncTimer = nvxio::createSyncTimer();
        syncTimer->arm(1. / app.getFPSLimit());

        nvx::Timer totalTimer;
        totalTimer.tic();
        double proc_ms = 0;

        while (!eventData.stop)
        {
            if (!eventData.pause)
            {
                //
                // Grab next frame
                //
                frameStatus = frameSource->fetch(currFrame);

                if (frameStatus == nvxio::FrameSource::TIMEOUT)
                    continue;

                if (frameStatus == nvxio::FrameSource::CLOSED)
                {
                    if (!frameSource->open())
                    {
                        std::cerr << "Failed to reopen the source" << std::endl;
                        break;
                    }

                    do
                    {
                        frameStatus = frameSource->fetch(prevFrame);
                    } while (frameStatus == nvxio::FrameSource::TIMEOUT);
                    if (frameStatus == nvxio::FrameSource::CLOSED)
                    {
                        std::cerr << "Source has no frames" << std::endl;
                        return nvxio::Application::APP_EXIT_CODE_NO_FRAMESOURCE;
                    }

                    ime.init(prevFrame, currFrame, params);

                    continue;
                }

                //
                // Process
                //

                nvx::Timer procTimer;
                procTimer.tic();

                ime.process();

                proc_ms = procTimer.toc();
            }

            double total_ms = totalTimer.toc();

            // std::cout << "Display Time : " << total_ms << " ms" << std::endl << std::endl;

            syncTimer->synchronize();

            total_ms = totalTimer.toc();

            totalTimer.tic();

            //
            // Show performance statistics
            //

            if (!eventData.pause)
            {
 //               ime.printPerfs();
            }

            //
            // Render
            //
#if GST_SOURCE
			// Some debug
			// gstSource::dumpVxImage(prevFrame);
#endif
			vx_image motion = ime.getMotionField();

#if !HEADLESS
				render->putImage(prevFrame);

				nvxio::Render::MotionFieldStyle mfStyle = {
					{  0u, 255u, 255u, 255u} // color
				};

				render->putMotionField(motion, mfStyle);

				std::ostringstream msg;
				msg << std::fixed << std::setprecision(1);

				msg << "Resolution: " << frameConfig.frameWidth << 'x' << frameConfig.frameHeight << std::endl;
				msg << "Algorithm: " << proc_ms << " ms / " << 1000.0 / proc_ms << " FPS" << std::endl;
				msg << "Display: " << total_ms  << " ms / " << 1000.0 / total_ms << " FPS" << std::endl;
				msg << "Space - pause/resume" << std::endl;
				msg << "Esc - close the sample";

				nvxio::Render::TextBoxStyle textStyle = {
					{255u, 255u, 255u, 255u}, // color
					{0u,   0u,   0u, 127u}, // bgcolor
					{10u, 10u} // origin
				};

				render->putTextViewport(msg.str(), textStyle);
#endif
#if GST_RTP_SINK
			{
				vx_uint32 mb_width, mb_height;
				mb_width = frameConfig.frameWidth/2;
				mb_height = frameConfig.frameHeight/2;
				// Extract motion fields
				char* motion_buffer[mb_width * mb_height * sizeof(vx_float32)*2];
				const vx_rectangle_t motionrect = { 0, 0, mb_width, mb_height};
				const vx_imagepatch_addressing_t src_addr = {
					mb_width,
					mb_height,
					sizeof(vx_float32)*2,
					(int)(mb_width * sizeof(vx_float32)*2),
					VX_SCALE_UNITY,
					VX_SCALE_UNITY,
					1,
					1 };

				status = vxCopyImagePatch(motion,
									&motionrect,
									0,
									&src_addr,
									motion_buffer,
									VX_READ_ONLY,
									VX_MEMORY_TYPE_HOST
				);
				if (status != VX_SUCCESS)
				{
					printf("[MOTION] vxCopyImagePatch motion data failed!\n");
					return nvxio::Application::APP_EXIT_CODE_ERROR;
				}

#if GST_SOURCE
				// Transmit the RTP video our over Ethernet
				// RGB data is already on the GPU
				void *gpuBuffer = frameSource->getLastGPUBuffer();

				cudaMotionFields((uint8_t*)gpuBuffer, (vx_float32*)motion_buffer, frameConfig.frameWidth, frameConfig.frameHeight);
				rtpStreaming.Transmit((char*)gpuBuffer, true);
			}
#else
				//
				// Transmit the RTP video our over Ethernet
				//
				int result;

				char roi[frameConfig.frameWidth * frameConfig.frameHeight * 4];

//printf(">>>>>>>>>>>> %d %d\n", frameConfig.frameWidth, frameConfig.frameHeight);

				const vx_rectangle_t rect = { 0, 0, frameConfig.frameWidth, frameConfig.frameHeight};
				const vx_imagepatch_addressing_t src_addr = {
					frameConfig.frameWidth,
					frameConfig.frameHeight,

					sizeof(vx_uint8)*4,
					frameConfig.frameWidth * sizeof(vx_uint8)*4,

					VX_SCALE_UNITY,
					VX_SCALE_UNITY,
					1,
					1 };

				{
					//
					// Extract raw RGBX image from prevFrame. Need to do
					// this for video streaming from a file as we dont
					// have the video on the GPU already.
					//
					vxCopyImagePatch(	prevFrame,
										&rect,
										0,
										&src_addr,
										roi,
										VX_READ_ONLY,
										VX_MEMORY_TYPE_HOST
					);
				}
				rtpStreaming.Transmit(roi, false);
			}
#endif
#endif

#if !HEADLESS
            if (!render->flush())
            {
                eventData.stop = true;
            }
#endif
            if (!eventData.pause)
            {
                vxAgeDelay(frame_delay);
            }
        }

        //
        // Release all objects
        //

        vxReleaseDelay(&frame_delay);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return nvxio::Application::APP_EXIT_CODE_ERROR;
    }

    return nvxio::Application::APP_EXIT_CODE_SUCCESS;
}
