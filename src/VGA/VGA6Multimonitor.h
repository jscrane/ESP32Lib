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
#include "../Graphics/BufferLayouts/BLpx6sz8swmx2yshmxy.h"

class VGA6Multimonitor : public VGAI2SEngine<BLpx1sz8sw2sh0>, public Graphics<ColorW1X7, BLpx6sz8swmx2yshmxy, CTBIdentity>
{
  public:
	VGA6Multimonitor() //8 bit based modes only work with I2S1
		: VGAI2SEngine<BLpx1sz8sw2sh0>(1)
	{
		frontColor = 0x1;
	}

	bool init(const Mode &mode,
			  const int M0Pin, const int M1Pin,
			  const int M2Pin, const int M3Pin,
			  const int M4Pin, const int M5Pin,
			  const int hsyncPin, const int vsyncPin, const int clockPin = -1,
			  const int horMonitorCount = 3, const int verMonitorCount = 2)
	{
		const int bitCount = 8;
		int pinMap[bitCount] = {
			M0Pin, M1Pin,
			M2Pin, M3Pin,
			M4Pin, M5Pin,
			hsyncPin, vsyncPin
		};
		mx = horMonitorCount;
		my = verMonitorCount;

		return initoverlappingbuffers(mode, pinMap, bitCount, clockPin);
	}

	bool init(const Mode &mode,
			const int *pinMap, const int *pinMapBit, const int pinCount,
			const int clockPin = -1,
			const int horMonitorCount = 3, const int verMonitorCount = 2)
	{
		const int bitCount = 8;
		mx = horMonitorCount;
		my = verMonitorCount;

		return initoverlappingbuffers(mode, pinMap, pinMapBit, pinCount, bitCount, clockPin);
	}

	bool init(const Mode &mode, const PinConfig &pinConfig)
	{
		return init(mode, pinConfig, 3, 2);
	}

	bool init(const Mode &mode, const PinConfig &pinConfig, const int horMonitorCount = 3, const int verMonitorCount = 2)
	{
		const int bitCount = 8;
		int pinMap[bitCount];
		pinConfig.fill6Bit(pinMap);
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
	
	bool initMulti(const Mode &mode, const int *pinMap, const int *pinMapBit, const int pinCount, const int bitCount, const int clockPin, int descriptorsPerLine = 2)
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
		initParallelOutputMode(pinMap, pinMapBit, pinCount, mode.pixelClock, bitCount, clockPin);
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

	virtual void clear(Color color = 0)
	{
		BufferGraphicsUnit newColor = (BufferGraphicsUnit)BLpx1sz8sw2sh0::static_shval(Graphics<ColorW1X7, BLpx6sz8swmx2yshmxy, CTBIdentity>::graphics_coltobuf(color & Graphics<ColorW1X7, BLpx6sz8swmx2yshmxy, CTBIdentity>::graphics_colormask(), 0, 0), 0, 0);
		for (int y = 0; y < this->wy; y++)
			for (int x = 0; x < this->wx; x++)
				backBuffer[y][x] &= 0b11000000;//newColor;
	}

	bool initoverlappingbuffers(const Mode &mode, const int *pinMap, const int bitCount, const int clockPin = -1)
	{
		lineBufferCount = mode.vRes / mode.vDiv; // yres
		rendererBufferCount = frameBufferCount;
		return initMulti(mode, pinMap, bitCount, clockPin, 2); // 2 buffers per line
	}

	bool initoverlappingbuffers(const Mode &mode, const int *pinMap, const int *pinMapBit, const int pinCount, const int bitCount, const int clockPin = -1)
	{
		lineBufferCount = mode.vRes / mode.vDiv; // yres
		rendererBufferCount = frameBufferCount;
		return initMulti(mode, pinMap, pinMapBit, pinCount, bitCount, clockPin, 2); // 2 buffers per line
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
