/*
Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/


#include <kernels_rpp.h>
#include <vx_ext_rpp.h>
#include <stdio.h>
#include "internal_rpp.h"

#include "internal_publishKernels.h"

#include </opt/rocm/rpp/include/rpp.h>
#include </opt/rocm/rpp/include/rppdefs.h>
#include </opt/rocm/rpp/include/rppi.h>

struct WarpAffineLocalData {

#if ENABLE_OPENCL
    RPPCommonHandle handle;
#endif
    RppiSize srcDim;
    RppiSize dstDim;
    RppPtr_t pSrc;
    RppPtr_t pDst;
    Rpp32f affine[6];
    Rpp32u device_type;
#if ENABLE_OPENCL
    cl_mem cl_pSrc;
    cl_mem cl_pDst;
#endif

#if ENABLE_HIP
    void *hip_pSrc;
    void *hip_pDst;
#endif

};

static vx_status VX_CALLBACK validateWarpAffine(vx_node node, const vx_reference parameters[], vx_uint32 num, vx_meta_format metas[])
{
 // check scalar alpha and beta type
    vx_status status = VX_SUCCESS;
    vx_enum type;
    // Param 1 is output 
    vx_parameter param = vxGetParameterByIndex(node, 0);
    STATUS_ERROR_CHECK(vxQueryScalar((vx_scalar)parameters[2], VX_SCALAR_TYPE, &type, sizeof(type)));
    if(type != VX_TYPE_INT32) return ERRMSG(VX_ERROR_INVALID_TYPE, "validate: Paramter: #2 type=%d (must be size)\n", type);
    STATUS_ERROR_CHECK(vxQueryScalar((vx_scalar)parameters[3], VX_SCALAR_TYPE, &type, sizeof(type)));
    if(type != VX_TYPE_INT32) return ERRMSG(VX_ERROR_INVALID_TYPE, "validate: Paramter: #3 type=%d (must be size)\n", type);
    STATUS_ERROR_CHECK(vxQueryScalar((vx_scalar)parameters[4], VX_SCALAR_TYPE, &type, sizeof(type)));
    if(type != VX_TYPE_FLOAT32) return ERRMSG(VX_ERROR_INVALID_TYPE, "validate: Paramter: #4 type=%d (must be size)\n", type);
    STATUS_ERROR_CHECK(vxQueryScalar((vx_scalar)parameters[5], VX_SCALAR_TYPE, &type, sizeof(type)));
    if(type != VX_TYPE_FLOAT32) return ERRMSG(VX_ERROR_INVALID_TYPE, "validate: Paramter: #5 type=%d (must be size)\n", type);
    STATUS_ERROR_CHECK(vxQueryScalar((vx_scalar)parameters[6], VX_SCALAR_TYPE, &type, sizeof(type)));
    if(type != VX_TYPE_FLOAT32) return ERRMSG(VX_ERROR_INVALID_TYPE, "validate: Paramter: #6 type=%d (must be size)\n", type);
    STATUS_ERROR_CHECK(vxQueryScalar((vx_scalar)parameters[7], VX_SCALAR_TYPE, &type, sizeof(type)));
    if(type != VX_TYPE_FLOAT32) return ERRMSG(VX_ERROR_INVALID_TYPE, "validate: Paramter: #7 type=%d (must be size)\n", type);
    STATUS_ERROR_CHECK(vxQueryScalar((vx_scalar)parameters[8], VX_SCALAR_TYPE, &type, sizeof(type)));
    if(type != VX_TYPE_FLOAT32) return ERRMSG(VX_ERROR_INVALID_TYPE, "validate: Paramter: #8 type=%d (must be size)\n", type);
    STATUS_ERROR_CHECK(vxQueryScalar((vx_scalar)parameters[9], VX_SCALAR_TYPE, &type, sizeof(type)));
    if(type != VX_TYPE_FLOAT32) return ERRMSG(VX_ERROR_INVALID_TYPE, "validate: Paramter: #9 type=%d (must be size)\n", type);

    vx_image image;
    vx_df_image df_image = VX_DF_IMAGE_VIRT;
    STATUS_ERROR_CHECK(vxQueryParameter(param, VX_PARAMETER_ATTRIBUTE_REF, &image, sizeof(vx_image)));
    STATUS_ERROR_CHECK(vxQueryImage(image, VX_IMAGE_ATTRIBUTE_FORMAT, &df_image, sizeof(df_image)));
    if (df_image != VX_DF_IMAGE_U8 && df_image != VX_DF_IMAGE_RGB)
        status = VX_ERROR_INVALID_VALUE;

    STATUS_ERROR_CHECK(vxSetMetaFormatAttribute(metas[1], VX_IMAGE_FORMAT, &df_image, sizeof(df_image)));


    // Param 1 is output
    vx_parameter output_param = vxGetParameterByIndex(node, 1);
    vx_image output_image;
    STATUS_ERROR_CHECK(vxQueryParameter(output_param, VX_PARAMETER_ATTRIBUTE_REF, &output_image, sizeof(vx_image)));
    vx_uint32  height, width;
    STATUS_ERROR_CHECK(vxQueryImage(output_image, VX_IMAGE_ATTRIBUTE_HEIGHT, &height, sizeof(height)));
    STATUS_ERROR_CHECK(vxSetMetaFormatAttribute(metas[1], VX_IMAGE_HEIGHT, &height, sizeof(height)));

    STATUS_ERROR_CHECK(vxQueryImage(output_image, VX_IMAGE_ATTRIBUTE_WIDTH, &width, sizeof(width)));
    STATUS_ERROR_CHECK(vxSetMetaFormatAttribute(metas[1], VX_IMAGE_WIDTH, &width, sizeof(width)));
	vxReleaseImage(&image);
    vxReleaseImage(&output_image);
    
    return status;
}

static vx_status VX_CALLBACK processWarpAffine(vx_node node, const vx_reference * parameters, vx_uint32 num)
{
    WarpAffineLocalData * data = NULL;
    STATUS_ERROR_CHECK(vxQueryNode(node, VX_NODE_LOCAL_DATA_PTR, &data, sizeof(data)));
    vx_df_image df_image = VX_DF_IMAGE_VIRT;
    STATUS_ERROR_CHECK(vxQueryImage((vx_image)parameters[0], VX_IMAGE_ATTRIBUTE_FORMAT, &df_image, sizeof(df_image)));
    if(data->device_type == AGO_TARGET_AFFINITY_GPU) {
#if ENABLE_OPENCL

    cl_command_queue handle = data->handle.cmdq;
    STATUS_ERROR_CHECK(vxQueryImage((vx_image)parameters[0], VX_IMAGE_HEIGHT, &data->srcDim.height, sizeof(data->srcDim.height)));
    STATUS_ERROR_CHECK(vxQueryImage((vx_image)parameters[0], VX_IMAGE_WIDTH, &data->srcDim.width, sizeof(data->srcDim.width)));
    STATUS_ERROR_CHECK(vxQueryImage((vx_image)parameters[0], VX_IMAGE_ATTRIBUTE_AMD_OPENCL_BUFFER, &data->cl_pSrc, sizeof(data->cl_pSrc)));
    STATUS_ERROR_CHECK(vxQueryImage((vx_image)parameters[1], VX_IMAGE_ATTRIBUTE_AMD_OPENCL_BUFFER, &data->cl_pDst, sizeof(data->cl_pDst)));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[2], &data->dstDim.width));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[3], &data->dstDim.height));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[4], &data->affine[0]));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[5], &data->affine[1]));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[6], &data->affine[2]));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[7], &data->affine[3]));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[8], &data->affine[4]));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[9], &data->affine[5]));
    if (df_image == VX_DF_IMAGE_U8 ){
        rppi_warp_affine_u8_pln1_gpu((void *)data->cl_pSrc, data->srcDim, (void*)data->cl_pDst, data->dstDim ,(Rpp32f *) data->affine, (void *)handle);
    }
    else if(df_image == VX_DF_IMAGE_RGB) {
        rppi_warp_affine_u8_pkd3_gpu((void *)data->cl_pSrc, data->srcDim, (void*)data->cl_pDst, data->dstDim, (Rpp32f *)data->affine, (void *)handle);
    }
    return VX_SUCCESS;

#endif
    } else if(data->device_type == AGO_TARGET_AFFINITY_CPU) {
    STATUS_ERROR_CHECK(vxQueryImage((vx_image)parameters[0], VX_IMAGE_HEIGHT, &data->srcDim.height, sizeof(data->srcDim.height)));
    STATUS_ERROR_CHECK(vxQueryImage((vx_image)parameters[0], VX_IMAGE_WIDTH, &data->srcDim.width, sizeof(data->srcDim.width)));
    STATUS_ERROR_CHECK(vxQueryImage((vx_image)parameters[0], VX_IMAGE_ATTRIBUTE_AMD_HOST_BUFFER, &data->pSrc, sizeof(vx_uint8)));
    STATUS_ERROR_CHECK(vxQueryImage((vx_image)parameters[1], VX_IMAGE_ATTRIBUTE_AMD_HOST_BUFFER, &data->pDst, sizeof(vx_uint8)));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[2], &data->dstDim.width));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[3], &data->dstDim.height));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[4], &data->affine[0]));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[5], &data->affine[1]));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[6], &data->affine[2]));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[7], &data->affine[3]));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[8], &data->affine[4]));
    STATUS_ERROR_CHECK(vxReadScalarValue((vx_scalar)parameters[9], &data->affine[5]));
    if (df_image == VX_DF_IMAGE_U8 ){
        rppi_warp_affine_u8_pln1_host(data->pSrc, data->srcDim, data->pDst, data->dstDim ,data->affine);
    }
    else if(df_image == VX_DF_IMAGE_RGB) {
        rppi_warp_affine_u8_pkd3_host(data->pSrc, data->srcDim, data->pDst, data->dstDim, data->affine);
    }
    return VX_SUCCESS;
    }
}

static vx_status VX_CALLBACK initializeWarpAffine(vx_node node, const vx_reference *parameters, vx_uint32 num)
{
    WarpAffineLocalData * data = new WarpAffineLocalData;
    memset(data, 0, sizeof(*data));

#if ENABLE_OPENCL
    STATUS_ERROR_CHECK(vxQueryNode(node, VX_NODE_ATTRIBUTE_AMD_OPENCL_COMMAND_QUEUE, &data->handle.cmdq, sizeof(data->handle.cmdq)));
#endif

    STATUS_ERROR_CHECK(vxQueryImage((vx_image)parameters[0], VX_IMAGE_HEIGHT, &data->srcDim.height, sizeof(data->srcDim.height)));
    STATUS_ERROR_CHECK(vxQueryImage((vx_image)parameters[0], VX_IMAGE_WIDTH, &data->srcDim.width, sizeof(data->srcDim.width)));
    STATUS_ERROR_CHECK(vxCopyScalar((vx_scalar)parameters[2], &data->dstDim.width, VX_READ_ONLY, VX_MEMORY_TYPE_HOST));
    STATUS_ERROR_CHECK(vxCopyScalar((vx_scalar)parameters[3], &data->dstDim.height, VX_READ_ONLY, VX_MEMORY_TYPE_HOST));
    STATUS_ERROR_CHECK(vxCopyScalar((vx_scalar)parameters[4], &data->affine[0], VX_READ_ONLY, VX_MEMORY_TYPE_HOST));
    STATUS_ERROR_CHECK(vxCopyScalar((vx_scalar)parameters[5], &data->affine[1], VX_READ_ONLY, VX_MEMORY_TYPE_HOST))
    STATUS_ERROR_CHECK(vxCopyScalar((vx_scalar)parameters[6], &data->affine[2], VX_READ_ONLY, VX_MEMORY_TYPE_HOST));
    STATUS_ERROR_CHECK(vxCopyScalar((vx_scalar)parameters[7], &data->affine[3], VX_READ_ONLY, VX_MEMORY_TYPE_HOST))
    STATUS_ERROR_CHECK(vxCopyScalar((vx_scalar)parameters[8], &data->affine[4], VX_READ_ONLY, VX_MEMORY_TYPE_HOST));
    STATUS_ERROR_CHECK(vxCopyScalar((vx_scalar)parameters[9], &data->affine[5], VX_READ_ONLY, VX_MEMORY_TYPE_HOST));
    STATUS_ERROR_CHECK(vxCopyScalar((vx_scalar)parameters[10], &data->device_type, VX_READ_ONLY, VX_MEMORY_TYPE_HOST));

#if ENABLE_OPENCL
    STATUS_ERROR_CHECK(vxQueryImage((vx_image)parameters[0], VX_IMAGE_ATTRIBUTE_AMD_OPENCL_BUFFER, &data->cl_pSrc, sizeof(data->cl_pSrc)));
#else
    //STATUS_ERROR_CHECK(vxQueryImage((vx_image)parameters[0], VX_IMAGE_ATTRIBUTE_AMD_HOST_BUFFER, data->pSrc, sizeof(data->pSrc)));
#endif

    STATUS_ERROR_CHECK(vxSetNodeAttribute(node, VX_NODE_LOCAL_DATA_PTR, &data, sizeof(data)));

    return VX_SUCCESS;
}

static vx_status VX_CALLBACK uninitializeWarpAffine(vx_node node, const vx_reference *parameters, vx_uint32 num)
{
    return VX_SUCCESS;
}

vx_status WarpAffine_Register(vx_context context)
{
	vx_status status = VX_SUCCESS;
// add kernel to the context with callbacks
    vx_kernel kernel = vxAddUserKernel(context, "org.rpp.WarpAffine",
            VX_KERNEL_RPP_WARP_AFFINE,
            processWarpAffine,
            11,
            validateWarpAffine,
            initializeWarpAffine,
            uninitializeWarpAffine);

    ERROR_CHECK_OBJECT(kernel);
    AgoTargetAffinityInfo affinity;
    vxQueryContext(context, VX_CONTEXT_ATTRIBUTE_AMD_AFFINITY,&affinity, sizeof(affinity));
    #if ENABLE_OPENCL
    // enable OpenCL buffer access since the kernel_f callback uses OpenCL buffers instead of host accessible buffers
    vx_bool enableBufferAccess = vx_true_e;
    if(affinity.device_type == AGO_TARGET_AFFINITY_GPU)
        STATUS_ERROR_CHECK(vxSetKernelAttribute(kernel, VX_KERNEL_ATTRIBUTE_AMD_OPENCL_BUFFER_ACCESS_ENABLE, &enableBufferAccess, sizeof(enableBufferAccess)));
    #else
    vx_bool enableBufferAccess = vx_false_e;
    #endif
    if (kernel)
    {
        PARAM_ERROR_CHECK(vxAddParameterToKernel(kernel, 0, VX_INPUT, VX_TYPE_IMAGE, VX_PARAMETER_STATE_REQUIRED));
        PARAM_ERROR_CHECK(vxAddParameterToKernel(kernel, 1, VX_OUTPUT, VX_TYPE_IMAGE, VX_PARAMETER_STATE_REQUIRED));
        PARAM_ERROR_CHECK(vxAddParameterToKernel(kernel, 2, VX_INPUT, VX_TYPE_SCALAR, VX_PARAMETER_STATE_REQUIRED));
        PARAM_ERROR_CHECK(vxAddParameterToKernel(kernel, 3, VX_INPUT, VX_TYPE_SCALAR, VX_PARAMETER_STATE_REQUIRED));
        PARAM_ERROR_CHECK(vxAddParameterToKernel(kernel, 4, VX_INPUT, VX_TYPE_SCALAR, VX_PARAMETER_STATE_REQUIRED));
        PARAM_ERROR_CHECK(vxAddParameterToKernel(kernel, 5, VX_INPUT, VX_TYPE_SCALAR, VX_PARAMETER_STATE_REQUIRED));
        PARAM_ERROR_CHECK(vxAddParameterToKernel(kernel, 6, VX_INPUT, VX_TYPE_SCALAR, VX_PARAMETER_STATE_REQUIRED));
        PARAM_ERROR_CHECK(vxAddParameterToKernel(kernel, 7, VX_INPUT, VX_TYPE_SCALAR, VX_PARAMETER_STATE_REQUIRED));
        PARAM_ERROR_CHECK(vxAddParameterToKernel(kernel, 8, VX_INPUT, VX_TYPE_SCALAR, VX_PARAMETER_STATE_REQUIRED));
        PARAM_ERROR_CHECK(vxAddParameterToKernel(kernel, 9, VX_INPUT, VX_TYPE_SCALAR, VX_PARAMETER_STATE_REQUIRED));
        PARAM_ERROR_CHECK(vxAddParameterToKernel(kernel, 10, VX_INPUT, VX_TYPE_SCALAR, VX_PARAMETER_STATE_REQUIRED));
        PARAM_ERROR_CHECK(vxFinalizeKernel(kernel));
    }

    if (status != VX_SUCCESS)
    {
    exit:	vxRemoveKernel(kernel); return VX_FAILURE;
    }

    return status;
}
