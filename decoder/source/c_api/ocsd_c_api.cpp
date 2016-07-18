/*
 * \file       ocsd_c_api.cpp
 * \brief      OpenCSD : "C" API libary implementation.
 * 
 * \copyright  Copyright (c) 2015, ARM Limited. All Rights Reserved.
 */

/* 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * 
 * 3. Neither the name of the copyright holder nor the names of its contributors 
 * may be used to endorse or promote products derived from this software without 
 * specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 'AS IS' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND 
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */ 

#include <cstring>

/* pull in the C++ decode library */
#include "opencsd.h"

/* C-API and wrapper objects */
#include "c_api/opencsd_c_api.h"
#include "ocsd_c_api_obj.h"

/** MSVC2010 unwanted export workaround */
#ifdef WIN32
#if (_MSC_VER == 1600)
#include <new>
namespace std { const nothrow_t nothrow = nothrow_t(); }
#endif
#endif

/*******************************************************************************/
/* C API internal helper function declarations                                 */
/*******************************************************************************/

static ocsd_err_t ocsd_create_pkt_sink_cb(ocsd_trace_protocol_t protocol, FnDefPktDataIn pPktInFn, const void *p_context, ITrcTypedBase **ppCBObj );
static ocsd_err_t ocsd_create_pkt_mon_cb(ocsd_trace_protocol_t protocol, FnDefPktDataMon pPktInFn, const void *p_context, ITrcTypedBase **ppCBObj );

/*******************************************************************************/
/* C library data - additional data on top of the C++ library objects          */
/*******************************************************************************/

/* keep a list of interface objects for a decode tree for later disposal */
typedef struct _lib_dt_data_list {
    std::vector<ITrcTypedBase *> cb_objs;
} lib_dt_data_list;

/* map lists to handles */
static std::map<dcd_tree_handle_t, lib_dt_data_list *> s_data_map;

/*******************************************************************************/
/* C API functions                                                             */
/*******************************************************************************/

/** Get Library version. Return a 32 bit version in form MMMMnnnn - MMMM = major verison, nnnn = minor version */ 
OCSD_C_API uint32_t ocsd_get_version(void) 
{ 
    return ocsdVersion::vers_num();
}

/** Get library version string */
OCSD_C_API const char * ocsd_get_version_str(void) 
{ 
    return ocsdVersion::vers_str();
}


/*** Decode tree creation etc. */

OCSD_C_API dcd_tree_handle_t ocsd_create_dcd_tree(const ocsd_dcd_tree_src_t src_type, const uint32_t deformatterCfgFlags)
{
    dcd_tree_handle_t handle = C_API_INVALID_TREE_HANDLE;
    handle = (dcd_tree_handle_t)DecodeTree::CreateDecodeTree(src_type,deformatterCfgFlags); 
    if(handle != C_API_INVALID_TREE_HANDLE)
    {
        lib_dt_data_list *pList = new (std::nothrow) lib_dt_data_list;
        if(pList != 0)
        {
            s_data_map.insert(std::pair<dcd_tree_handle_t, lib_dt_data_list *>(handle,pList));
        }
        else
        {
            ocsd_destroy_dcd_tree(handle);
            handle = C_API_INVALID_TREE_HANDLE;
        }
    }
    return handle;
}

OCSD_C_API void ocsd_destroy_dcd_tree(const dcd_tree_handle_t handle)
{
    if(handle != C_API_INVALID_TREE_HANDLE)
    {
        GenTraceElemCBObj * pIf = (GenTraceElemCBObj *)(((DecodeTree *)handle)->getGenTraceElemOutI());
        if(pIf != 0)
            delete pIf;

        /* need to clear any associated callback data. */
        std::map<dcd_tree_handle_t, lib_dt_data_list *>::iterator it;
        it = s_data_map.find(handle);
        if(it != s_data_map.end())
        {
            std::vector<ITrcTypedBase *>::iterator itcb;
            itcb = it->second->cb_objs.begin();
            while(itcb != it->second->cb_objs.end())
            {
                delete *itcb;
                itcb++;
            }
            it->second->cb_objs.clear();
            delete it->second;
            s_data_map.erase(it);
        }
        DecodeTree::DestroyDecodeTree((DecodeTree *)handle);
    }
}

/*** Decode tree process data */

OCSD_C_API ocsd_datapath_resp_t ocsd_dt_process_data(const dcd_tree_handle_t handle,
                                            const ocsd_datapath_op_t op,
                                            const ocsd_trc_index_t index,
                                            const uint32_t dataBlockSize,
                                            const uint8_t *pDataBlock,
                                            uint32_t *numBytesProcessed)
{
    ocsd_datapath_resp_t resp =  OCSD_RESP_FATAL_NOT_INIT;
    if(handle != C_API_INVALID_TREE_HANDLE)
        resp = ((DecodeTree *)handle)->TraceDataIn(op,index,dataBlockSize,pDataBlock,numBytesProcessed);
    return resp;
}

/*** Decode tree - decoder management */

OCSD_C_API ocsd_err_t ocsd_dt_create_decoder(const dcd_tree_handle_t handle,
                                             const char *decoder_name,
                                             const int create_flags,
                                             const void *decoder_cfg,
                                             unsigned char *pCSID
                                             )
{
    ocsd_err_t err = OCSD_OK;
    DecodeTree *dt = (DecodeTree *)handle;
    std::string dName = decoder_name;
    IDecoderMngr *pDcdMngr;
    err = OcsdLibDcdRegister::getDecoderRegister()->getDecoderMngrByName(dName,&pDcdMngr);
    if(err != OCSD_OK)
        return err;

    CSConfig *pConfig = 0;
    err = pDcdMngr->createConfigFromDataStruct(&pConfig,decoder_cfg);
    if(err != OCSD_OK)
        return err;

    err = dt->createDecoder(dName,create_flags,pConfig);
    if(err == OCSD_OK)
        *pCSID = pConfig->getTraceID();
    delete pConfig;
    return err;
}

OCSD_C_API ocsd_err_t ocsd_dt_remove_decoder(   const dcd_tree_handle_t handle, 
                                                const unsigned char CSID)
{
    return ((DecodeTree *)handle)->removeDecoder(CSID);
}

OCSD_C_API ocsd_err_t ocsd_dt_attach_packet_callback(  const dcd_tree_handle_t handle, 
                                                const unsigned char CSID,
                                                const ocsd_c_api_cb_types callback_type, 
                                                void *p_fn_callback_data,
                                                const void *p_context)
{
    ocsd_err_t err = OCSD_OK;
    DecodeTree *pDT = static_cast<DecodeTree *>(handle);
    DecodeTreeElement *pElem = pDT->getDecoderElement(CSID);
    if(pElem == 0)
        return OCSD_ERR_INVALID_ID;  // cannot find entry for that CSID

    ITrcTypedBase *pDataInSink = 0;  // pointer to a sink callback object
    switch(callback_type)
    {
    case OCSD_C_API_CB_PKT_SINK:
        err = ocsd_create_pkt_sink_cb(pElem->getProtocol(),(FnDefPktDataIn)p_fn_callback_data,p_context,&pDataInSink);
        break;

    case OCSD_C_API_CB_PKT_MON:
        err = ocsd_create_pkt_mon_cb(pElem->getProtocol(),(FnDefPktDataMon)p_fn_callback_data,p_context,&pDataInSink);
        break;

    default:
        err = OCSD_ERR_INVALID_PARAM_VAL;
    }

    if(err == OCSD_OK)
    {
        err = pElem->getDecoderMngr()->attachPktSink(pElem->getDecoderHandle(),pDataInSink);
        if(err == OCSD_OK)
        {
            // save object pointer for destruction later.
            std::map<dcd_tree_handle_t, lib_dt_data_list *>::iterator it;
            it = s_data_map.find(handle);
            if(it != s_data_map.end())
                it->second->cb_objs.push_back(pDataInSink);
        }
    }
    return err;
}

/*** Decode tree set element output */

OCSD_C_API ocsd_err_t ocsd_dt_set_gen_elem_outfn(const dcd_tree_handle_t handle, FnTraceElemIn pFn, const void *p_context)
{

    GenTraceElemCBObj * pCBObj = new (std::nothrow)GenTraceElemCBObj(pFn, p_context);
    if(pCBObj)
    {
        ((DecodeTree *)handle)->setGenTraceElemOutI(pCBObj);
        return OCSD_OK;
    }
    return OCSD_ERR_MEM;
}


/*** Default error logging */

OCSD_C_API ocsd_err_t ocsd_def_errlog_init(const ocsd_err_severity_t verbosity, const int create_output_logger)
{
    if(DecodeTree::getDefaultErrorLogger()->initErrorLogger(verbosity,(bool)(create_output_logger != 0)))
        return OCSD_OK;
    return OCSD_ERR_NOT_INIT;
}

OCSD_C_API ocsd_err_t ocsd_def_errlog_config_output(const int output_flags, const char *log_file_name)
{
    ocsdMsgLogger *pLogger = DecodeTree::getDefaultErrorLogger()->getOutputLogger();
    if(pLogger)
    {
        pLogger->setLogOpts(output_flags & C_API_MSGLOGOUT_MASK);
        if(log_file_name != NULL)
        {
            pLogger->setLogFileName(log_file_name);
        }
        return OCSD_OK;
    }
    return OCSD_ERR_NOT_INIT;    
}

OCSD_C_API void ocsd_def_errlog_msgout(const char *msg)
{
    ocsdMsgLogger *pLogger = DecodeTree::getDefaultErrorLogger()->getOutputLogger();
    if(pLogger)
        pLogger->LogMsg(msg);
}

/*** Convert packet to string */

OCSD_C_API ocsd_err_t ocsd_pkt_str(const ocsd_trace_protocol_t pkt_protocol, const void *p_pkt, char *buffer, const int buffer_size)
{
    ocsd_err_t err = OCSD_OK;
    if((buffer == NULL) || (buffer_size < 2))
        return OCSD_ERR_INVALID_PARAM_VAL;

    std::string pktStr = "";
    buffer[0] = 0;

    switch(pkt_protocol)
    {
    case OCSD_PROTOCOL_ETMV4I:
        trcPrintElemToString<EtmV4ITrcPacket,ocsd_etmv4_i_pkt>(static_cast<const ocsd_etmv4_i_pkt *>(p_pkt), pktStr);
        //EtmV4ITrcPacket::toString(static_cast<ocsd_etmv4_i_pkt *>(p_pkt), pktStr);
        break;

    case OCSD_PROTOCOL_ETMV3:
        trcPrintElemToString<EtmV3TrcPacket,ocsd_etmv3_pkt>(static_cast<const ocsd_etmv3_pkt *>(p_pkt), pktStr);
        break;

    case OCSD_PROTOCOL_STM:
        trcPrintElemToString<StmTrcPacket,ocsd_stm_pkt>(static_cast<const ocsd_stm_pkt *>(p_pkt), pktStr);
        break;

    case OCSD_PROTOCOL_PTM:
        trcPrintElemToString<PtmTrcPacket,ocsd_ptm_pkt>(static_cast<const ocsd_ptm_pkt *>(p_pkt), pktStr);
        break;

    default:
        err = OCSD_ERR_NO_PROTOCOL;
        break;
    }

    if(pktStr.size() > 0)
    {
        strncpy(buffer,pktStr.c_str(),buffer_size-1);
        buffer[buffer_size-1] = 0;
    }
    return err;
}

OCSD_C_API ocsd_err_t ocsd_gen_elem_str(const ocsd_generic_trace_elem *p_pkt, char *buffer, const int buffer_size)
{
    ocsd_err_t err = OCSD_OK;
    if((buffer == NULL) || (buffer_size < 2))
        return OCSD_ERR_INVALID_PARAM_VAL;
    std::string str;
    trcPrintElemToString<OcsdTraceElement,ocsd_generic_trace_elem>(p_pkt,str);
    if(str.size() > 0)
    {
        strncpy(buffer,str.c_str(),buffer_size -1);
        buffer[buffer_size-1] = 0;
    }
    return err;
}


/*** Decode tree -- memeory accessor control */

OCSD_C_API ocsd_err_t ocsd_dt_add_binfile_mem_acc(const dcd_tree_handle_t handle, const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, const char *filepath)
{
    ocsd_err_t err = OCSD_OK;

    if(handle != C_API_INVALID_TREE_HANDLE)
    {
        DecodeTree *pDT = static_cast<DecodeTree *>(handle);
        if(!pDT->hasMemAccMapper())
            err = pDT->createMemAccMapper();

        if(err == OCSD_OK)
        {
            TrcMemAccessorBase *p_accessor;
            std::string pathToFile = filepath;
            err = TrcMemAccFactory::CreateFileAccessor(&p_accessor,pathToFile,address);            
            if(err == OCSD_OK)
            {
                TrcMemAccessorFile *pAcc = dynamic_cast<TrcMemAccessorFile *>(p_accessor);
                if(pAcc)
                {
                    pAcc->setMemSpace(mem_space);
                    err = pDT->addMemAccessorToMap(pAcc,0);
                }
                else
                    err = OCSD_ERR_MEM;    // wrong type of object - treat as mem error

                if(err != OCSD_OK)
                    TrcMemAccFactory::DestroyAccessor(p_accessor);
            }
        }
    }
    else
        err = OCSD_ERR_INVALID_PARAM_VAL;
    return err;
}

OCSD_C_API ocsd_err_t ocsd_dt_add_binfile_region_mem_acc(const dcd_tree_handle_t handle, const file_mem_region_t *region_array, const int num_regions, const ocsd_mem_space_acc_t mem_space, const char *filepath)
{
    ocsd_err_t err = OCSD_OK;

    if((handle != C_API_INVALID_TREE_HANDLE) && (region_array != 0) && (num_regions != 0))
    {
        DecodeTree *pDT = static_cast<DecodeTree *>(handle);
        if(!pDT->hasMemAccMapper())
            err = pDT->createMemAccMapper();

        if(err == OCSD_OK)
        {
            TrcMemAccessorBase *p_accessor;
            std::string pathToFile = filepath;
            int curr_region_idx = 0;
            err = TrcMemAccFactory::CreateFileAccessor(&p_accessor,pathToFile,region_array[curr_region_idx].start_address,region_array[curr_region_idx].file_offset, region_array[curr_region_idx].region_size);            
            if(err == OCSD_OK)
            {
                TrcMemAccessorFile *pAcc = dynamic_cast<TrcMemAccessorFile *>(p_accessor);
                if(pAcc)
                {
                    curr_region_idx++;
                    while(curr_region_idx < num_regions)
                    {
                        pAcc->AddOffsetRange(region_array[curr_region_idx].start_address, 
                                             region_array[curr_region_idx].region_size,
                                             region_array[curr_region_idx].file_offset);
                        curr_region_idx++;
                    }
                    pAcc->setMemSpace(mem_space);
                    err = pDT->addMemAccessorToMap(pAcc,0);
                }
                else
                    err = OCSD_ERR_MEM;    // wrong type of object - treat as mem error

                if(err != OCSD_OK)
                    TrcMemAccFactory::DestroyAccessor(p_accessor);
            }
        }
    }
    else
        err = OCSD_ERR_INVALID_PARAM_VAL;
    return err;
}

OCSD_C_API ocsd_err_t ocsd_dt_add_buffer_mem_acc(const dcd_tree_handle_t handle, const ocsd_vaddr_t address, const ocsd_mem_space_acc_t mem_space, const uint8_t *p_mem_buffer, const uint32_t mem_length)
{
    ocsd_err_t err = OCSD_OK;

    if(handle != C_API_INVALID_TREE_HANDLE)
    {
        DecodeTree *pDT = static_cast<DecodeTree *>(handle);
        if(!pDT->hasMemAccMapper())
            err = pDT->createMemAccMapper();

        if(err == OCSD_OK)
        {
            TrcMemAccessorBase *p_accessor;
            err = TrcMemAccFactory::CreateBufferAccessor(&p_accessor, address, p_mem_buffer, mem_length);
            if(err == OCSD_OK)
            {
                TrcMemAccBufPtr *pMBuffAcc = dynamic_cast<TrcMemAccBufPtr *>(p_accessor);
                if(pMBuffAcc)
                {
                    pMBuffAcc->setMemSpace(mem_space);
                    err = pDT->addMemAccessorToMap(p_accessor,0);
                }
                else
                    err = OCSD_ERR_MEM;    // wrong type of object - treat as mem error

                if(err != OCSD_OK)
                    TrcMemAccFactory::DestroyAccessor(p_accessor);
            }
        }
    }
    else
        err = OCSD_ERR_INVALID_PARAM_VAL;
    return err;
}

OCSD_C_API ocsd_err_t ocsd_dt_add_callback_mem_acc(const dcd_tree_handle_t handle, const ocsd_vaddr_t st_address, const ocsd_vaddr_t en_address, const ocsd_mem_space_acc_t mem_space, Fn_MemAcc_CB p_cb_func, const void *p_context)
{
    ocsd_err_t err = OCSD_OK;

    if(handle != C_API_INVALID_TREE_HANDLE)
    {
        DecodeTree *pDT = static_cast<DecodeTree *>(handle);
        if(!pDT->hasMemAccMapper())
            err = pDT->createMemAccMapper();

        if(err == OCSD_OK)
        {
            TrcMemAccessorBase *p_accessor;
            err = TrcMemAccFactory::CreateCBAccessor(&p_accessor, st_address, en_address, mem_space);
            if(err == OCSD_OK)
            {
                TrcMemAccCB *pCBAcc = dynamic_cast<TrcMemAccCB *>(p_accessor);
                if(pCBAcc)
                {
                    pCBAcc->setCBIfFn(p_cb_func, p_context);
                    err = pDT->addMemAccessorToMap(p_accessor,0);
                }
                else
                    err = OCSD_ERR_MEM;    // wrong type of object - treat as mem error

                if(err != OCSD_OK)
                    TrcMemAccFactory::DestroyAccessor(p_accessor);
            }
        }
    }
    else
        err = OCSD_ERR_INVALID_PARAM_VAL;
    return err;
}

OCSD_C_API ocsd_err_t ocsd_dt_remove_mem_acc(const dcd_tree_handle_t handle, const ocsd_vaddr_t st_address, const ocsd_mem_space_acc_t mem_space)
{
    ocsd_err_t err = OCSD_OK;

    if(handle != C_API_INVALID_TREE_HANDLE)
    {
        DecodeTree *pDT = static_cast<DecodeTree *>(handle);
        if(!pDT->hasMemAccMapper())
            err = OCSD_ERR_INVALID_PARAM_VAL; /* no mapper, no remove*/
        else
            err = pDT->removeMemAccessorByAddress(st_address,mem_space,0);
    }
    else
        err = OCSD_ERR_INVALID_PARAM_VAL;
    return err;
}

OCSD_C_API void ocsd_tl_log_mapped_mem_ranges(const dcd_tree_handle_t handle)
{
    if(handle != C_API_INVALID_TREE_HANDLE)
    {
        DecodeTree *pDT = static_cast<DecodeTree *>(handle);
        pDT->logMappedRanges();
    }
}

/*******************************************************************************/
/* C API local fns                                                             */
/*******************************************************************************/
static ocsd_err_t ocsd_create_pkt_sink_cb(ocsd_trace_protocol_t protocol,  FnDefPktDataIn pPktInFn, const void *p_context, ITrcTypedBase **ppCBObj )
{
    ocsd_err_t err = OCSD_OK;
    *ppCBObj = 0;

    switch(protocol)
    {
    case OCSD_PROTOCOL_ETMV4I:
        *ppCBObj = new (std::nothrow) PktCBObj<EtmV4ITrcPacket,ocsd_etmv4_i_pkt>(pPktInFn,p_context); 
        break;

    case OCSD_PROTOCOL_ETMV3:
        *ppCBObj = new (std::nothrow) PktCBObj<EtmV3TrcPacket,ocsd_etmv3_pkt>(pPktInFn,p_context); 
        break;

    case OCSD_PROTOCOL_PTM:
        *ppCBObj = new (std::nothrow) PktCBObj<PtmTrcPacket,ocsd_ptm_pkt>(pPktInFn,p_context); 
        break;

    case OCSD_PROTOCOL_STM:
        *ppCBObj = new (std::nothrow) PktCBObj<StmTrcPacket,ocsd_stm_pkt>(pPktInFn,p_context); 
        break;

    default:
        err = OCSD_ERR_NO_PROTOCOL;
        break;
    }

    if((*ppCBObj == 0) && (err == OCSD_OK))
        err = OCSD_ERR_MEM;

    return err;
}

static ocsd_err_t ocsd_create_pkt_mon_cb(ocsd_trace_protocol_t protocol, FnDefPktDataMon pPktInFn, const void *p_context, ITrcTypedBase **ppCBObj )
{
    ocsd_err_t err = OCSD_OK;
    *ppCBObj = 0;

    switch(protocol)
    {
    case OCSD_PROTOCOL_ETMV4I:
        *ppCBObj = new (std::nothrow) PktMonCBObj<EtmV4ITrcPacket,ocsd_etmv4_i_pkt>(pPktInFn,p_context); 
        break;

    case OCSD_PROTOCOL_ETMV3:
        *ppCBObj = new (std::nothrow) PktMonCBObj<EtmV3TrcPacket,ocsd_etmv3_pkt>(pPktInFn,p_context); 
        break;

    case OCSD_PROTOCOL_PTM:
        *ppCBObj = new (std::nothrow) PktMonCBObj<PtmTrcPacket,ocsd_ptm_pkt>(pPktInFn,p_context); 
        break;

    case OCSD_PROTOCOL_STM:
        *ppCBObj = new (std::nothrow) PktMonCBObj<StmTrcPacket,ocsd_stm_pkt>(pPktInFn,p_context); 
        break;

    default:
        err = OCSD_ERR_NO_PROTOCOL;
        break;
    }

    if((*ppCBObj == 0) && (err == OCSD_OK))
        err = OCSD_ERR_MEM;

    return err;
}


/*******************************************************************************/
/* C API Helper objects                                                        */
/*******************************************************************************/

/****************** Generic trace element output callback function  ************/
GenTraceElemCBObj::GenTraceElemCBObj(FnTraceElemIn pCBFn, const void *p_context) :
    m_c_api_cb_fn(pCBFn),
    m_p_cb_context(p_context)
{
}

ocsd_datapath_resp_t GenTraceElemCBObj::TraceElemIn(const ocsd_trc_index_t index_sop,
                                              const uint8_t trc_chan_id,
                                              const OcsdTraceElement &elem)
{
    return m_c_api_cb_fn(m_p_cb_context, index_sop, trc_chan_id, &elem);
}


#if 0

/****************** Etmv4 packet processor output callback function  ************/
EtmV4ICBObj::EtmV4ICBObj(FnEtmv4IPacketDataIn pCBFn, const void *p_context) :
    m_c_api_cb_fn(pCBFn),
    m_p_cb_context(p_context)
{
}

ocsd_datapath_resp_t EtmV4ICBObj::PacketDataIn( const ocsd_datapath_op_t op,
                                                 const ocsd_trc_index_t index_sop,
                                                 const EtmV4ITrcPacket *p_packet_in)
{
    return m_c_api_cb_fn(m_p_cb_context, op,index_sop,p_packet_in);
}

/****************** Etmv4 packet processor monitor callback function  ***********/
EtmV4IPktMonCBObj::EtmV4IPktMonCBObj(FnEtmv4IPktMonDataIn pCBFn, const void *p_context) :
    m_c_api_cb_fn(pCBFn),
    m_p_cb_context(p_context)
{
}
    
void EtmV4IPktMonCBObj::RawPacketDataMon( const ocsd_datapath_op_t op,
                                   const ocsd_trc_index_t index_sop,
                                   const EtmV4ITrcPacket *p_packet_in,
                                   const uint32_t size,
                                   const uint8_t *p_data)
{
    return m_c_api_cb_fn(m_p_cb_context, op, index_sop, p_packet_in, size, p_data);
}

/****************** Etmv3 packet processor output callback function  ************/
EtmV3CBObj::EtmV3CBObj(FnEtmv3PacketDataIn pCBFn, const void *p_context) :
    m_c_api_cb_fn(pCBFn),
    m_p_cb_context(p_context)
{
}

ocsd_datapath_resp_t EtmV3CBObj::PacketDataIn( const ocsd_datapath_op_t op,
                                                 const ocsd_trc_index_t index_sop,
                                                 const EtmV3TrcPacket *p_packet_in)
{
    return m_c_api_cb_fn(m_p_cb_context, op,index_sop,*p_packet_in);
}

/****************** Etmv3 packet processor monitor callback function  ***********/
EtmV3PktMonCBObj::EtmV3PktMonCBObj(FnEtmv3PktMonDataIn pCBFn, const void *p_context) :
    m_c_api_cb_fn(pCBFn),
    m_p_cb_context(p_context)
{
}
    
void EtmV3PktMonCBObj::RawPacketDataMon( const ocsd_datapath_op_t op,
                                   const ocsd_trc_index_t index_sop,
                                   const EtmV3TrcPacket *p_packet_in,
                                   const uint32_t size,
                                   const uint8_t *p_data)
{
    return m_c_api_cb_fn(m_p_cb_context, op, index_sop, *p_packet_in, size, p_data);
}


/****************** Ptm packet processor output callback function  ************/
PtmCBObj::PtmCBObj(FnPtmPacketDataIn pCBFn, const void *p_context) :
    m_c_api_cb_fn(pCBFn),
    m_p_cb_context(p_context)
{
}

ocsd_datapath_resp_t PtmCBObj::PacketDataIn( const ocsd_datapath_op_t op,
                                                 const ocsd_trc_index_t index_sop,
                                                 const PtmTrcPacket *p_packet_in)
{
    return m_c_api_cb_fn(m_p_cb_context, op,index_sop,p_packet_in);
}

/****************** Ptm packet processor monitor callback function  ***********/
PtmPktMonCBObj::PtmPktMonCBObj(FnPtmPktMonDataIn pCBFn, const void *p_context) :
    m_c_api_cb_fn(pCBFn),
    m_p_cb_context(p_context)
{
}
    
void PtmPktMonCBObj::RawPacketDataMon( const ocsd_datapath_op_t op,
                                   const ocsd_trc_index_t index_sop,
                                   const PtmTrcPacket *p_packet_in,
                                   const uint32_t size,
                                   const uint8_t *p_data)
{
    return m_c_api_cb_fn(m_p_cb_context, op, index_sop, p_packet_in, size, p_data);
}

/****************** Stm packet processor output callback function  ************/
StmCBObj::StmCBObj(FnStmPacketDataIn pCBFn, const void *p_context) :
    m_c_api_cb_fn(pCBFn),
    m_p_cb_context(p_context)
{
}

ocsd_datapath_resp_t StmCBObj::PacketDataIn( const ocsd_datapath_op_t op,
                                                 const ocsd_trc_index_t index_sop,
                                                 const StmTrcPacket *p_packet_in)
{
    return m_c_api_cb_fn(m_p_cb_context, op,index_sop,p_packet_in);
}

/****************** Stm packet processor monitor callback function  ***********/
StmPktMonCBObj::StmPktMonCBObj(FnStmPktMonDataIn pCBFn, const void *p_context) :
    m_c_api_cb_fn(pCBFn),
    m_p_cb_context(p_context)
{
}
    
void StmPktMonCBObj::RawPacketDataMon( const ocsd_datapath_op_t op,
                                   const ocsd_trc_index_t index_sop,
                                   const StmTrcPacket *p_packet_in,
                                   const uint32_t size,
                                   const uint8_t *p_data)
{
    return m_c_api_cb_fn(m_p_cb_context, op, index_sop, p_packet_in, size, p_data);
}
#endif

/* End of File ocsd_c_api.cpp */
