﻿#include "CreateFlowMap.h"

ACreateFlowMap::ACreateFlowMap(const class FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	m_coloredGradient = false;
	b_smoothHeights = false;
}


void ACreateFlowMap::CreateMap()
{
	TArray<TArray<float>> waterMap;
	waterMap.SetNum(m_width);
	for (int x = 0; x < waterMap.Num();) {
		waterMap[x].SetNum(m_height);
		x++;
	}

	TArray<TArray<TArray<float>>> outFlow;
	outFlow.SetNum(m_width);
	for (int x = 0; x < outFlow.Num();) {
		outFlow[x].SetNum(m_height);

		for (int y = 0; y < outFlow[x].Num();) {
			outFlow[x][y].SetNum(4);
			y++;
		}
		x++;
	}

	FillWaterMap(0.0001f, waterMap, m_width, m_height);

	for (int i = 0; i < m_iterations; i++)
	{
		ComputeOutflow(waterMap, outFlow, m_heights, m_width, m_height);
		UpdateWaterMap(waterMap, outFlow, m_width, m_height);
	}

	TArray<TArray<float>> velocityMap;
	velocityMap.SetNum(m_width);
	for (int x = 0; x < velocityMap.Num();) {
		velocityMap[x].SetNum(m_height);
		x++;
	}

	CalculateVelocityField(velocityMap, outFlow, m_width, m_height);
	NormalizeMap(velocityMap, m_width, m_height);

	UTexture2D* flowMap = CreateTexture(m_width, m_height);
	// Create base mip.
	uint8* DestPtr = static_cast<uint8*>(flowMap->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE));

	for (int y = 0; y < m_height; y++)
	{
		for (int x = 0; x < m_width; x++)
		{
			float v = velocityMap[x][y];
			FLinearColor color(v, v, v, 1);
			FColor UEColor = color.ToFColor(false);
			//flowMap->SetPixel(x, y, &tempVar);
			*DestPtr++ = UEColor.B;
			*DestPtr++ = UEColor.G;
			*DestPtr++ = UEColor.R;
			*DestPtr++ = UEColor.A;
		}
	}

	flowMap->PlatformData->Mips[0].BulkData.Unlock();
	flowMap->UpdateResource();
	//flowMap->Apply();
	t_output = flowMap;

}

void ACreateFlowMap::FillWaterMap(float amount, TArray<TArray<float>>& waterMap, int width, int height)
{
	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			waterMap[x][y] = amount;
		}
	}
}

void ACreateFlowMap::ComputeOutflow(TArray<TArray<float>>& waterMap, TArray<TArray<TArray<float>>>& outFlow, TArray<float>& heightMap, int width, int height)
{

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			int xn1 = (x == 0) ? 0 : x - 1;
			int xp1 = (x == width - 1) ? width - 1 : x + 1;
			int yn1 = (y == 0) ? 0 : y - 1;
			int yp1 = (y == height - 1) ? height - 1 : y + 1;

			float waterHt = waterMap[x][y];
			float waterHts0 = waterMap[xn1][y];
			float waterHts1 = waterMap[xp1][y];
			float waterHts2 = waterMap[x][yn1];
			float waterHts3 = waterMap[x][yp1];

			float landHt = heightMap[x + y * width];
			float landHts0 = heightMap[xn1 + y * width];
			float landHts1 = heightMap[xp1 + y * width];
			float landHts2 = heightMap[x + yn1 * width];
			float landHts3 = heightMap[x + yp1 * width];

			float diff0 = (waterHt + landHt) - (waterHts0 + landHts0);
			float diff1 = (waterHt + landHt) - (waterHts1 + landHts1);
			float diff2 = (waterHt + landHt) - (waterHts2 + landHts2);
			float diff3 = (waterHt + landHt) - (waterHts3 + landHts3);

			//out flow is previous flow plus flow for this time step.
			float flow0 = FMath::Max(0.f, outFlow[x][y][0] + diff0);
			float flow1 = FMath::Max(0.f, outFlow[x][y][1] + diff1);
			float flow2 = FMath::Max(0.f, outFlow[x][y][2] + diff2);
			float flow3 = FMath::Max(0.f, outFlow[x][y][3] + diff3);

			float sum = flow0 + flow1 + flow2 + flow3;

			if (sum > 0.0f)
			{
				//If the sum of the outflow flux exceeds the amount in the cell
				//flow value will be scaled down by a factor K to avoid negative update.
				float K = waterHt / (sum * TIME);
				if (K > 1.0f)
				{
					K = 1.0f;
				}
				if (K < 0.0f)
				{
					K = 0.0f;
				}

				outFlow[x][y][0] = flow0 * K;
				outFlow[x][y][1] = flow1 * K;
				outFlow[x][y][2] = flow2 * K;
				outFlow[x][y][3] = flow3 * K;
			}
			else
			{
				outFlow[x][y][0] = 0.0f;
				outFlow[x][y][1] = 0.0f;
				outFlow[x][y][2] = 0.0f;
				outFlow[x][y][3] = 0.0f;
			}

		}
	}

}

void ACreateFlowMap::UpdateWaterMap(TArray<TArray<float>>& waterMap, TArray<TArray<TArray<float>>>& outFlow, int width, int height)
{

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			float flowOUT = outFlow[x][y][0] + outFlow[x][y][1] + outFlow[x][y][2] + outFlow[x][y][3];
			float flowIN = 0.0f;

			//Flow in is inflow from neighour cells. Note for the cell on the left you need 
			//thats cells flow to the right (ie it flows into this cell)
			flowIN += (x == 0) ? 0.0f : outFlow[x - 1][y][RIGHT];
			flowIN += (x == width - 1) ? 0.0f : outFlow[x + 1][y][LEFT];
			flowIN += (y == 0) ? 0.0f : outFlow[x][y - 1][TOP];
			flowIN += (y == height - 1) ? 0.0f : outFlow[x][y + 1][BOTTOM];

			float ht = waterMap[x][y] + (flowIN - flowOUT) * TIME;
			if (ht < 0.0f)
			{
				ht = 0.0f;
			}

			//Result is net volume change over time
			waterMap[x][y] = ht;
		}
	}

}

void ACreateFlowMap::CalculateVelocityField(TArray<TArray<float>>& velocityMap, TArray<TArray<TArray<float>>>& outFlow, int width, int height)
{

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			float dl = (x == 0) ? 0.0f : outFlow[x - 1][y][RIGHT] - outFlow[x][y][LEFT];

			float dr = (x == width - 1) ? 0.0f : outFlow[x][y][RIGHT] - outFlow[x + 1][y][LEFT];

			float dt = (y == height - 1) ? 0.0f : outFlow[x][y + 1][BOTTOM] - outFlow[x][y][TOP];

			float db = (y == 0) ? 0.0f : outFlow[x][y][BOTTOM] - outFlow[x][y - 1][TOP];

			float vx = (dl + dr) * 0.5f;
			float vy = (db + dt) * 0.5f;

			velocityMap[x][y] = FMath::Sqrt(vx * vx + vy * vy);
		}

	}

}

void ACreateFlowMap::NormalizeMap(TArray<TArray<float>>& map, int width, int height)
{

	float min = INFINITY;
	float max = -INFINITY;

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			float v = map[x][y];
			if (v < min)
			{
				min = v;
			}
			if (v > max)
			{
				max = v;
			}
		}
	}

	float size = max - min;

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			float v = map[x][y];

			if (size < 1e-12f)
			{
				v = 0;
			}
			else
			{
				v = (v - min) / size;
			}

			map[x][y] = v;
		}
	}

}