/* osgCompute - Copyright (C) 2008-2009 SVT Group
*                                                                     
* This library is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as
* published by the Free Software Foundation; either version 3 of
* the License, or (at your option) any later version.
*                                                                     
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of 
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesse General Public License for more details.
*
* The full license is in LICENSE file included with this distribution.
*/

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DEVICE FUNCTIONS //////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
inline __device__ 
float lerp(float a, float b, float t)
{
    return a + t*(b-a);
}

//------------------------------------------------------------------------------
inline __device__ 
float4 reseed( float* seeds, unsigned int seedCount, unsigned int seedIdx, unsigned int ptclIdx, float3 bbmin, float3 bbmax )
{
    // random seed idx
    unsigned int idx1 = (seedIdx + ptclIdx) % seedCount;
    unsigned int idx2 = (idx1 + ptclIdx) % seedCount;
    unsigned int idx3 = (idx2 + ptclIdx) % seedCount;

    // seeds are within the range [0,1]
    float intFac1 = seeds[idx1];
    float intFac2 = seeds[idx2];
    float intFac3 = seeds[idx3];

    return make_float4(lerp(bbmin.x,bbmax.x,intFac1), lerp(bbmin.y,bbmax.y,intFac3),
        lerp(bbmin.z,bbmax.z,intFac2), 1);
}

//------------------------------------------------------------------------------
inline __device__
unsigned int thIdx()
{
    unsigned int x = blockIdx.x * blockDim.x + threadIdx.x;
    unsigned int y = blockIdx.y * blockDim.y + threadIdx.y;
    unsigned int width = gridDim.x * blockDim.x;

    return y*width + x;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GLOBAL FUNCTIONS //////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
__global__
void emitKernel(  unsigned int numPtcls, float4* ptcls, float* seeds, unsigned int seedIdx, float3 bbmin, float3 bbmax )
{
    unsigned int ptclIdx = thIdx();
    if( ptclIdx < numPtcls )
    {
        float4 curPtcl = ptcls[ptclIdx];

        if( curPtcl.x < bbmin.x ||
            curPtcl.y < bbmin.y ||
            curPtcl.z < bbmin.z ||
            curPtcl.x > bbmax.x ||
            curPtcl.y > bbmax.y ||
            curPtcl.z > bbmax.z )
            ptcls[ptclIdx] = reseed( seeds, numPtcls, seedIdx, ptclIdx, bbmin, bbmax );
    }
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HOST FUNCTIONS ////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
extern "C" __host__
void emit(unsigned int numPtcls, 
            void* ptcls, 
            void* seeds, 
            unsigned int seedIdx, 
            float3 bbmin, 
            float3 bbmax )
{
    dim3 blocks( (numPtcls / 128)+1, 1, 1 );
    dim3 threads( 128, 1, 1 );

    emitKernel<<< blocks, threads >>>(
        numPtcls,
        reinterpret_cast<float4*>(ptcls),
        reinterpret_cast<float*>(seeds),
        seedIdx,
        bbmin,
        bbmax);
}
