/*
	Author: Martin-Laclaustra 2021
	License: 
	Creative Commons Attribution ShareAlike 4.0
	https://creativecommons.org/licenses/by-sa/4.0/
	
	For further details check out: 
		https://youtube.com/bitlunislab
		https://github.com/bitluni
		http://bitluni.net
*/
#pragma once
#include "VGAI2SEngine.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/BufferLayouts/BLpx4sz16swmx1yshmxy.h"

class VGA4ColorMultimonitor : public VGAI2SEngine<BLpx1sz16sw1sh0>, public Graphics<ColorR1G1B1A1X4, BLpx4sz16swmx1yshmxy, CTBIdentity>
{
  public:
	VGA4ColorMultimonitor(const int i2sIndex = 1)
		: VGAI2SEngine<BLpx1sz16sw1sh0>(i2sIndex)
	{
		frontColor = 0xf;
	}

	bool init(const Mode &mode,
			  const int R0Pin, const int G0Pin, const int B0Pin,
			  const int R1Pin, const int G1Pin, const int B1Pin,
			  const int R2Pin, const int G2Pin, const int B2Pin,
			  const int R3Pin, const int G3Pin, const int B3Pin,
			  const int R4Pin, const int G4Pin,
			  const int hsyncPin, const int vsyncPin, const int clockPin = -1,
			  const int horMonitorCount = 2, const int verMonitorCount = 2)
	{
		const int bitCount = 16;
		int pinMap[bitCount] = {
			R0Pin, G0Pin, B0Pin,
			R1Pin, G1Pin, B1Pin,
			R2Pin, G2Pin, B2Pin,
			R3Pin, G3Pin, B3Pin,
			-1, -1,
			hsyncPin, vsyncPin
		};
		mx = horMonitorCount;
		my = verMonitorCount;

		return initoverlappingbuffers(mode, pinMap, bitCount, clockPin);
	}

	bool init(const Mode &mode, const PinConfig &pinConfig)
	{
		return init(mode, pinConfig, 3, 2);
	}

	bool init(const Mode &mode, const PinConfig &pinConfig, const int horMonitorCount = 2, const int verMonitorCount = 2)
	{
		const int bitCount = 16;
		int pinMap[bitCount];
		pinConfig.fill14Bit(pinMap);
		int clockPin = pinConfig.clock;
		mx = horMonitorCount;
		my = verMonitorCount;

		return initoverlappingbuffers(mode, pinMap, bitCount, clockPin);
	}

	bool initMulti(const Mode &mode, const int *pinMap, const int bitCount, const int clockPin, int descriptorsPerLine = 2)
	{
		this->mode = mode;
		int xres = mx * mode.hRes;
		int yres = my * mode.vRes / mode.vDiv;
		wx = mode.hRes;
		wy = mode.vRes / mode.vDiv;
		initSyncBits();
		this->vsyncPin = vsyncPin;
		this->hsyncPin = hsyncPin;
		totalLines = mode.linesPerField();
		if(descriptorsPerLine < 1 || descriptorsPerLine > 2) ERROR("Wrong number of descriptors per line");
		if(descriptorsPerLine == 1) allocateRendererBuffers1DescriptorsPerLine();
		if(descriptorsPerLine == 2) allocateRendererBuffers2DescriptorsPerLine();
		propagateResolution(xres, yres);
		//allocateLineBuffers();
		currentLine = 0;
		vSyncPassed = false;
		initParallelOutputMode(pinMap, mode.pixelClock, bitCount, clockPin);
		startTX();
		return true;
	}


	//THE REST OF THE FILE IS SHARED CODE BETWEEN 3BIT, 6BIT, AND 14BIT

	static const int bitMaskInRenderingBufferHSync()
	{
		return 1<<(8*bytesPerBufferUnit()-2);
	}

	static const int bitMaskInRenderingBufferVSync()
	{
		return 1<<(8*bytesPerBufferUnit()-1);
	}

	bool initoverlappingbuffers(const Mode &mode, const int *pinMap, const int bitCount, const int clockPin = -1)
	{
		lineBufferCount = mode.vRes / mode.vDiv; // yres
		rendererBufferCount = frameBufferCount;
		return initMulti(mode, pinMap, bitCount, clockPin, 2); // 2 buffers per line
	}

	virtual void initSyncBits()
	{
		hsyncBitI = mode.hSyncPolarity ? (bitMaskInRenderingBufferHSync()) : 0;
		vsyncBitI = mode.vSyncPolarity ? (bitMaskInRenderingBufferVSync()) : 0;
		hsyncBit = hsyncBitI ^ (bitMaskInRenderingBufferHSync());
		vsyncBit = vsyncBitI ^ (bitMaskInRenderingBufferVSync());
	}

	virtual long syncBits(bool hSync, bool vSync)
	{
		return ((hSync ? hsyncBit : hsyncBitI) | (vSync ? vsyncBit : vsyncBitI)) * rendererStaticReplicate32();
	}

	virtual void propagateResolution(const int xres, const int yres)
	{
		setResolution(xres, yres);
	}

	int currentBufferToAssign = 0;

	virtual BufferGraphicsUnit **allocateFrameBuffer()
	{
		void **arr = (void **)malloc(yres * sizeof(void *));
		if(!arr)
			ERROR("Not enough memory");
		for (int y = 0; y < yres; y++)
		{
			arr[y] = (void *)getBufferDescriptor(graphics_swy(y), currentBufferToAssign);
		}
		currentBufferToAssign++;
		return (BufferGraphicsUnit **)arr;
	}

	virtual void show(bool vSync = false)
	{
		if (!frameBufferCount)
			return;

		Graphics::show(vSync);
		switchToRendererBuffer(currentFrameBuffer);
		// wait at least one frame
		// else the switch does not take place for the display
		// until the frame is completed
		// and drawing starts in the backbuffer while still shown
		if (frameBufferCount == 2) // in triple buffer or single buffer this is not an issue
		{
			uint32_t timemark = micros();
			uint32_t framedurationinus = (uint64_t)mode.pixelsPerLine() * (uint64_t)mode.linesPerField() * (uint64_t)1000000 / (uint64_t)mode.pixelClock;
			while((micros() - timemark) < framedurationinus){delay(0);}
		}
	}

	virtual void scroll(int dy, Color color)
	{
		Graphics::scroll(dy, color);
		if(dmaBufferDescriptors)
			for (int i = 0; i < yres * mode.vDiv / my; i++)
				dmaBufferDescriptors[
						indexRendererDataBuffer[(currentFrameBuffer + frameBufferCount - 1) % frameBufferCount]
						 + i * descriptorsPerLine + descriptorsPerLine - 1
					].setBuffer(
							((uint8_t *) backBuffer[i / mode.vDiv]) - dataOffsetInLineInBytes
							,
							((descriptorsPerLine > 1)?mode.hRes:mode.pixelsPerLine()) * bytesPerBufferUnit()/samplesPerBufferUnit()
						);
	}
};
