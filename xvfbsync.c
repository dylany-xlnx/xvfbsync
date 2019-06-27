/*
* Copyright (C) 2013 - 2016  Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person
* obtaining a copy of this software and associated documentation
* files (the "Software"), to deal in the Software without restriction,
* including without limitation the rights to use, copy, modify, merge,
* publish, distribute, sublicense, and/or sell copies of the Software,
* and to permit persons to whom the Software is furnished to do so,
* subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in this
* Software without prior written authorization from Xilinx.
*
*/

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "xvfbsync.h"

#define MIN(a,b) ((a) < (b) ? a : b)

/* ************** */
/* xvfbsync queue */
/* ************** */

static void xvfbsync_queue_init (struct Queue* q) 
{
  if(!q)
    return;

  q->front = NULL;
  q->last = NULL;
  q->size = 0;
}

static LLP2Buf* xvfbsync_queue_front (struct Queue* q) 
{
  if (!q)
    return NULL;

  return q->front->buf;
}

static void xvfbsync_queue_pop (struct Queue* q) 
{
  if(!q || q->size == 0)
    return;

  q->size--;

  struct Node* temp = q->front;
  q->front = q->front->next;
  free(temp);
}

static void xvfbsync_queue_push (struct Queue* q, LLP2Buf* bufptr)
{
  if (!q)
    return;

  q->size++;

  if (q->front == NULL) {
    q->front = (struct Node*) calloc (1, sizeof (struct Node));
    q->front->buf = bufptr;
    q->front->next = NULL;
    q->last = q->front;
  } else {
    q->last->next = (struct Node*) calloc (1, sizeof (struct Node));
    q->last->next->buf = bufptr;
    q->last->next->next = NULL;
    q->last = q->last->next;
  }
}

static int xvfbsync_queue_empty (struct Queue* q) 
{
  if (!q)
    return -1;

  return q->size == 0;
}

/* *********************** */
/* xvfbsync syncIP helpers */
/* *********************** */

static void parseChanStatus (struct xvsfsync_stat* status, 
  struct ChannelStatus1* channelStatuses, int maxChannels, 
  int maxUsers, int maxBuffers)
{
  for (int channel = 0; channel < maxChannels; ++channel)
  {
    struct ChannelStatus1* channelStatus = &(channelStatuses[channel]);

    for (int buffer = 0; buffer < maxBuffers; ++buffer)
    {
      for (int user = 0; user < maxUsers; ++user)
        channelStatus->fbAvail[buffer][user] = status->fbdone[channel][buffer][user];
    }

    channelStatus->enable = status->enable[channel];
    channelStatus->syncError = status->sync_err[channel];
    channelStatus->watchdogError = status->wdg_err[channel];
    channelStatus->lumaDiffError = status->ldiff_err[channel];
    channelStatus->chromaDiffError = status->cdiff_err[channel];
  }
}

static void xvfbsync_syncIP_getLatestChanStatus(struct SyncIp1* syncIP)
{
  struct xvsfsync_stat chan_status;

  if (ioctl (syncIP->fd, XVSFSYNC_GET_CHAN_STATUS, &chan_status))
    printf ("Couldn't get sync ip channel status");
  parseChanStatus (&chan_status, syncIP->channelStatuses, 
    syncIP->maxChannels, syncIP->maxUsers, syncIP->maxBuffers);
}

static void xvfbsync_syncIP_resetStatus(struct SyncIp1* syncIP, int chanId)
{
  struct xvsfsync_clr_err clr;
  clr.channel_id = chanId;
  clr.sync_err = 1;
  clr.wdg_err = 1;
  clr.ldiff_err = 1;
  clr.cdiff_err = 1;

  if (ioctl (syncIP->fd, XVSFSYNC_CLR_CHAN_ERR, &clr))
    printf ("Couldnt reset status of channel %d", chanId);
}

static void xvfbsync_syncIP_enableChannel(struct SyncIp1* syncIP, int chanId)
{
  u8 chan = chanId;

  if (ioctl (syncIP->fd, XVSFSYNC_CHAN_ENABLE, (void*)(uintptr_t)chan))
    printf ("Couldn't enable channel %d\n", chanId);
}

static void xvfbsync_syncIP_disableChannel(struct SyncIp1* syncIP, int chanId)
{
  u8 chan = chanId;

  if (ioctl (syncIP->fd, XVSFSYNC_CHAN_DISABLE, (void*)(uintptr_t)chan))
    printf ("Couldn't disable channel %d\n", chanId);
}

static void xvfbsync_syncIP_addBuffer(struct SyncIp1* syncIP, struct xvsfsync_chan_config* fbConfig)
{
  if (ioctl (syncIP->fd, XVSFSYNC_SET_CHAN_CONFIG, fbConfig))
    printf ("Couldn't add buffer");
}

static void xvfbsync_syncIP_pollErrors(struct SyncIp1* syncIP, int timeout)
{
  /*
  AL_EDriverError retCode = ioctl (syncIP->fd, AL_POLL_MSG, &timeout);

  if (retCode == DRIVER_TIMEOUT)
    return;

  if (retCode != DRIVER_SUCCESS)
    printf ("Error while polling the errors. (driver error: %d)\n", retCode);

  pthread_mutex_lock (&(syncIP->mutex));
  xvfbsync_syncIP_getLatestChanStatus (syncIP);

  for (size_t i = 0; i < syncIP->maxChannels; ++i)
  {
    struct ChannelStatus1* status = &(syncIP->channelStatuses[i]);

    if(syncIP->eventListeners[i] && (status->syncError || status->watchdogError || 
      status->lumaDiffError || status->chromaDiffError))
    {
      syncIP->eventListeners[i] (status);
      xvfbsync_syncIP_resetStatus(syncIP, i);
    }
  }

  pthread_mutex_unlock (&(syncIP->mutex));*/
  return;
}

static void* xvfbsync_syncIP_pollingRoutine(void* arg)
{
  struct SyncIp1* syncIP = ((struct ThreadInfo*)arg)->syncIP;

  while(true)
  {
    pthread_mutex_lock (&(syncIP->mutex));
    if(syncIP->quit) {
      break;
    }
    pthread_mutex_unlock (&(syncIP->mutex));
    xvfbsync_syncIP_pollErrors(syncIP, 5000);
  }
  pthread_mutex_unlock (&(syncIP->mutex));
  xvfbsync_syncIP_pollErrors(syncIP, 0);
  free ((struct ThreadInfo*)arg);
  return NULL;
}

static void xvfbsync_syncIP_addListener(struct SyncIp1* syncIP, int chanId, void (*delegate)(struct ChannelStatus1*))
{
  pthread_mutex_lock (&(syncIP->mutex));
  syncIP->eventListeners[chanId] = delegate;
  pthread_mutex_unlock (&(syncIP->mutex));
}

static void xvfbsync_syncIP_removeListener(struct SyncIp1* syncIP, int chanId)
{
  pthread_mutex_lock (&(syncIP->mutex));
  syncIP->eventListeners[chanId] = NULL;
  pthread_mutex_unlock (&(syncIP->mutex));
}

static struct ChannelStatus1* xvfbsync_syncIP_getStatus(struct SyncIp1* syncIP, int chanId)
{ 
  pthread_mutex_lock (&(syncIP->mutex));
  xvfbsync_syncIP_getLatestChanStatus(syncIP);
  pthread_mutex_unlock (&(syncIP->mutex));
  return &(syncIP->channelStatuses[chanId]);
}

/* *************** */
/* xvfbsync syncIP */
/* *************** */

int xvfbsync_syncIP_getFreeChannel(struct SyncIp1* syncIP)
{
  pthread_mutex_lock (&(syncIP->mutex));
  xvfbsync_syncIP_getLatestChanStatus(syncIP);

  /* TODO(driver lowlat2 xilinx) give a non racy way to choose a free channel
   * For now we look if all the framebuffer of a channel are available to
   * decide if a channel is free or not
   */
  for(int channel = 0; channel < syncIP->maxChannels; ++channel)
  {
    bool isAvailable = true;

    for(int buffer = 0; buffer < syncIP->maxBuffers; ++buffer)
    {
      for(int user = 0; user < syncIP->maxUsers; ++user)
        isAvailable = isAvailable && syncIP->channelStatuses[channel].fbAvail[buffer][user];
    }

    if(isAvailable) {
      pthread_mutex_unlock (&(syncIP->mutex));
      return channel;
    }
  }

  pthread_mutex_unlock (&(syncIP->mutex));
  printf ("No channel available");
  return -1;
}

int xvfbsync_syncIP_populate (struct SyncIp1* syncIP, int fd)
{
  struct ThreadInfo* tInfo = calloc (1, sizeof(struct ThreadInfo));
  tInfo->syncIP = syncIP;
  syncIP->quit = false;
  syncIP->fd = fd;

  if (syncIP->fd == -1) {
    printf ("Couldn't open the sync ip\n");
    return -1;
  }

  struct xvsfsync_config config;
  
  if (ioctl (syncIP->fd, XVSFSYNC_GET_CFG, &config)) {
    printf ("Couldn't get sync ip configuration\n");
    return -1;
  }

  printf ("[fd: %d] mode: %s, channel number: %d\n", syncIP->fd, 
    config.encode ? "encode" : "decode", config.max_channels);
  syncIP->maxChannels = config.max_channels;
  syncIP->maxUsers = XVSFSYNC_IO;
  syncIP->maxBuffers = XVSFSYNC_BUF_PER_CHANNEL;
  syncIP->maxCores = XVSFSYNC_MAX_CORES;
  syncIP->channelStatuses = calloc (config.max_channels, sizeof (void*));
  syncIP->eventListeners = calloc (config.max_channels, sizeof (void*));

  if (pthread_mutex_init (&(syncIP->mutex), NULL)) {
    printf ("Couldn't intialize lock");
    return -1;
  }

  if (pthread_create (&(syncIP->pollingThread), NULL, &xvfbsync_syncIP_pollingRoutine, tInfo)) {
    printf ("Couldn't create thread");
    return -1;
  }

  return 0;
}

void xvfbsync_syncIP_depopulate (struct SyncIp1* syncIP)
{
  syncIP->quit = true;
  pthread_join (syncIP->pollingThread, NULL);
  pthread_mutex_destroy (&(syncIP->mutex));
  free (syncIP->channelStatuses);
  free (syncIP->eventListeners);
}

/* ************************* */
/* xvfbsync syncChan helpers */
/* ************************* */

static const TFourCCMapping FourCCMappings[] =
{
  // planar: 8b
  FOURCC_MAPPING(XVFBSYNC_FOURCC2('I', '4', '2', '0'), CHROMA_4_2_0, 8, FB_RASTER, C_ORDER_U_V, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('I', 'Y', 'U', 'V'), CHROMA_4_2_0, 8, FB_RASTER, C_ORDER_U_V, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('Y', 'V', '1', '2'), CHROMA_4_2_0, 8, FB_RASTER, C_ORDER_V_U, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('I', '4', '2', '2'), CHROMA_4_2_2, 8, FB_RASTER, C_ORDER_U_V, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('Y', 'V', '1', '6'), CHROMA_4_2_2, 8, FB_RASTER, C_ORDER_U_V, false, false)
  // planar: 10b
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('I', '0', 'A', 'L'), CHROMA_4_2_0, 10, FB_RASTER, C_ORDER_U_V, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('I', '2', 'A', 'L'), CHROMA_4_2_2, 10, FB_RASTER, C_ORDER_U_V, false, false)

  // semi-planar: 8b
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('N', 'V', '1', '2'), CHROMA_4_2_0, 8, FB_RASTER, C_ORDER_SEMIPLANAR, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('N', 'V', '1', '6'), CHROMA_4_2_2, 8, FB_RASTER, C_ORDER_SEMIPLANAR, false, false)
  // semi-planar: 10b
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('P', '0', '1', '0'), CHROMA_4_2_0, 10, FB_RASTER, C_ORDER_SEMIPLANAR, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('P', '2', '1', '0'), CHROMA_4_2_2, 10, FB_RASTER, C_ORDER_SEMIPLANAR, false, false)

  // monochrome
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('Y', '8', '0', '0'), CHROMA_4_0_0, 8, FB_RASTER, C_ORDER_NO_CHROMA, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('Y', '0', '1', '0'), CHROMA_4_0_0, 10, FB_RASTER, C_ORDER_NO_CHROMA, false, false)

  // tile : 64x4
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('T', '6', '0', '8'), CHROMA_4_2_0, 8, FB_TILE_64x4, C_ORDER_SEMIPLANAR, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('T', '6', '2', '8'), CHROMA_4_2_2, 8, FB_TILE_64x4, C_ORDER_SEMIPLANAR, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('T', '6', 'm', '8'), CHROMA_4_0_0, 8, FB_TILE_64x4, C_ORDER_NO_CHROMA, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('T', '6', '0', 'A'), CHROMA_4_2_0, 10, FB_TILE_64x4, C_ORDER_SEMIPLANAR, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('T', '6', '2', 'A'), CHROMA_4_2_2, 10, FB_TILE_64x4, C_ORDER_SEMIPLANAR, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('T', '6', 'm', 'A'), CHROMA_4_0_0, 10, FB_TILE_64x4, C_ORDER_NO_CHROMA, false, false)
  // tile : 32x4
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('T', '5', '0', '8'), CHROMA_4_2_0, 8, FB_TILE_32x4, C_ORDER_SEMIPLANAR, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('T', '5', '2', '8'), CHROMA_4_2_2, 8, FB_TILE_32x4, C_ORDER_SEMIPLANAR, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('T', '5', 'm', '8'), CHROMA_4_0_0, 8, FB_TILE_32x4, C_ORDER_NO_CHROMA, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('T', '5', '0', 'A'), CHROMA_4_2_0, 10, FB_TILE_32x4, C_ORDER_SEMIPLANAR, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('T', '5', '2', 'A'), CHROMA_4_2_2, 10, FB_TILE_32x4, C_ORDER_SEMIPLANAR, false, false)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('T', '5', 'm', 'A'), CHROMA_4_0_0, 10, FB_TILE_32x4, C_ORDER_NO_CHROMA, false, false)


  // 10b packed
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('X', 'V', '1', '0'), CHROMA_4_0_0, 10, FB_RASTER, C_ORDER_NO_CHROMA, false, true)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('X', 'V', '1', '5'), CHROMA_4_2_0, 10, FB_RASTER, C_ORDER_SEMIPLANAR, false, true)
  , FOURCC_MAPPING(XVFBSYNC_FOURCC2('X', 'V', '2', '0'), CHROMA_4_2_2, 10, FB_RASTER, C_ORDER_SEMIPLANAR, false, true)
};

static const int FourCCMappingSize = sizeof(FourCCMappings) / sizeof(FourCCMappings[0]);


static bool GetPicFormat(uint32_t tFourCC, TPicFormat* tPicFormat)
{
  const TFourCCMapping* pBeginMapping = &FourCCMappings[0];
  const TFourCCMapping* pEndMapping = pBeginMapping + FourCCMappingSize;

  for(const TFourCCMapping* pMapping = pBeginMapping; pMapping != pEndMapping; pMapping++)
  {
    if(pMapping->tfourCC == tFourCC)
    {
      *tPicFormat = pMapping->tPictFormat;
      return true;
    }
  }

  assert(0);

  return false;
}


bool IsTiled(uint32_t tFourCC)
{
  TPicFormat tPicFormat;
  return GetPicFormat(tFourCC, &tPicFormat) && (tPicFormat.eStorageMode != FB_RASTER);
}

EChromaMode GetChromaMode(uint32_t tFourCC)
{
  TPicFormat tPicFormat;
  return GetPicFormat(tFourCC, &tPicFormat) ? tPicFormat.eChromaMode : (EChromaMode) - 1;
}

bool IsSemiPlanar(uint32_t tFourCC)
{
  TPicFormat tPicFormat;
  return GetPicFormat(tFourCC, &tPicFormat) && (tPicFormat.eChromaOrder == C_ORDER_SEMIPLANAR);
}

bool IsMonochrome(uint32_t tFourCC)
{
  return GetChromaMode(tFourCC) == CHROMA_MONO;
}

static int RoundUp (int iVal, int iRnd)
{
  assert ((iRnd % 2) == 0);
  return (iVal + iRnd - 1) / iRnd * iRnd;
}
 
static void printFrameBufferConfig(struct xvsfsync_chan_config* config, int maxUsers, int maxCores)
{
  printf ("********************************\n");
  printf ("channel_id:%d\n", config->channel_id);
  printf ("luma_margin:%d\n", config->luma_margin);
  printf ("chroma_margin:%d\n", config->chroma_margin);

  for (int user = 0; user < maxUsers; ++user)
  {
    printf ("%s[%d]:\n", (user == XVSFSYNC_PROD) ? "prod" : (user == XVSFSYNC_CONS) ? "cons" : "unknown", user);
    printf ("\t-fb_id:%d %s\n", config->fb_id[user], config->fb_id[user] == XVSFSYNC_AUTO_SEARCH ? "(auto_search)" : "");
    printf ("\t-ismono:%s\n", (config->ismono[user] == 0) ? "false" : "true");
    printf ("\t-luma_start_address:%" PRIx64 "\n", config->luma_start_address[user]);
    printf ("\t-luma_end_address:%" PRIx64 "\n", config->luma_end_address[user]);
    printf ("\t-chroma_start_address:%" PRIx64 "\n", config->chroma_start_address[user]);
    printf ("\t-chroma_end_address:%" PRIx64 "\n", config->chroma_end_address[user]);
  }

  for (int core = 0; core < maxCores; ++core)
  {
    printf ("core[%i]:\n", core);
    printf ("\t-luma_core_offset:%d\n", config->luma_core_offset[core]);
    printf ("\t-chroma_core_offset:%d\n", config->chroma_core_offset[core]);
  }

  printf ("********************************\n");
}

static int xvsfsync_chan_getLumaSize(LLP2Buf* buf)
{
  if(IsTiled(buf->tFourCC))
    return buf->tPlanes[PLANE_Y].iPitch * buf->tDim.iHeight / 4;
  return buf->tPlanes[PLANE_Y].iPitch * buf->tDim.iHeight;
}

static int xvsfsync_chan_getChromaSize(LLP2Buf* buf)
{
  EChromaMode eCMode = GetChromaMode(buf->tFourCC);

  if(eCMode == CHROMA_MONO)
    return 0;

  int const iHeightC = (eCMode == CHROMA_4_2_0) ? buf->tDim.iHeight / 2 : buf->tDim.iHeight;

  if(IsTiled(buf->tFourCC))
    return buf->tPlanes[PLANE_UV].iPitch * iHeightC / 4;

  if(IsSemiPlanar(buf->tFourCC))
    return buf->tPlanes[PLANE_UV].iPitch * iHeightC;

  return buf->tPlanes[PLANE_UV].iPitch * iHeightC * 2;
}

static int xvsfsync_chan_getOffsetUV(LLP2Buf* buf)
{
  assert(buf->tPlanes[PLANE_Y].iPitch * buf->tDim.iHeight <= buf->tPlanes[PLANE_UV].iOffset ||
         (IsTiled(buf->tFourCC) &&
          (buf->tPlanes[PLANE_Y].iPitch * buf->tDim.iHeight / 4 <= buf->tPlanes[PLANE_UV].iOffset)));
  return buf->tPlanes[PLANE_UV].iOffset;
}

static struct xvsfsync_chan_config setEncFrameBufferConfig(int channelId, LLP2Buf* buf, int hardwareHorizontalStrideAlignment, int hardwareVerticalStrideAlignment)
{
  uint32_t physical = buf->phyAddr;

  struct xvsfsync_chan_config config;

  config.luma_start_address[XVSFSYNC_PROD] = physical + buf->tPlanes[PLANE_Y].iOffset;
  config.luma_end_address[XVSFSYNC_PROD] = config.luma_start_address[XVSFSYNC_PROD] + xvsfsync_chan_getLumaSize (buf) - buf->tPlanes[PLANE_Y].iPitch + buf->tDim.iWidth - 1;

  config.luma_start_address[XVSFSYNC_CONS] = physical + buf->tPlanes[PLANE_Y].iOffset;
  /*           <------------> stride
   *           <--------> width
   * height   ^
   *          |
   *          |
   *          v         x last pixel of the image
   * end = (height - 1) * stride + width - 1 (to get the last pixel of the image)
   * total_size = height * stride
   * end = total_size - stride + width - 1
   */
  int iHardwarePitch = RoundUp(buf->tPlanes[PLANE_Y].iPitch, hardwareHorizontalStrideAlignment);
  int iHardwareLumaVerticalPitch = RoundUp(buf->tDim.iHeight, hardwareVerticalStrideAlignment);
  config.luma_end_address[XVSFSYNC_CONS] = config.luma_start_address[XVSFSYNC_CONS] + (iHardwarePitch * (iHardwareLumaVerticalPitch - 1)) + RoundUp(buf->tDim.iWidth, hardwareHorizontalStrideAlignment) - 1;

  /* chroma is the same, but the width depends on the format of the yuv
   * here we make the assumption that the fourcc is semi planar */
  if(!IsMonochrome(buf->tFourCC))
  {
    assert(IsSemiPlanar(buf->tFourCC));
    config.chroma_start_address[XVSFSYNC_PROD] = physical + xvsfsync_chan_getOffsetUV (buf);
    config.chroma_end_address[XVSFSYNC_PROD] = config.chroma_start_address[XVSFSYNC_PROD] + xvsfsync_chan_getChromaSize (buf) - buf->tPlanes[PLANE_UV].iPitch + buf->tDim.iWidth - 1;
    config.chroma_start_address[XVSFSYNC_CONS] = physical + xvsfsync_chan_getOffsetUV (buf);
    int iVerticalFactor = (GetChromaMode(buf->tFourCC) == CHROMA_4_2_0) ? 2 : 1;
    int iHardwareChromaVerticalPitch = RoundUp((buf->tDim.iHeight / iVerticalFactor), (hardwareVerticalStrideAlignment / iVerticalFactor));
    config.chroma_end_address[XVSFSYNC_CONS] = config.chroma_start_address[XVSFSYNC_CONS] + (iHardwarePitch * (iHardwareChromaVerticalPitch - 1)) + RoundUp(buf->tDim.iWidth, hardwareHorizontalStrideAlignment) - 1;
  }
  else
  {
    for(int user = 0; user < XVSFSYNC_IO; user++)
    {
      config.chroma_start_address[user] = 0;
      config.chroma_end_address[user] = 0;
      config.ismono[user] = 1;
    }
  }

  for(int core = 0; core < XVSFSYNC_MAX_CORES; core++)
  {
    config.luma_core_offset[core] = 0;
    config.chroma_core_offset[core] = 0;
  }

  /* no margin for now (only needed for the decoder) */
  config.luma_margin = 0;
  config.chroma_margin = 0;

  config.fb_id[XVSFSYNC_PROD] = XVSFSYNC_AUTO_SEARCH;
  config.fb_id[XVSFSYNC_CONS] = XVSFSYNC_AUTO_SEARCH;
  config.channel_id = channelId;

  return config;
}

static struct xvsfsync_chan_config setDecFrameBufferConfig(int channelId, LLP2Buf* buf)
{
  uint32_t physical = buf->phyAddr;

  struct xvsfsync_chan_config config;

  config.luma_start_address[XVSFSYNC_PROD] = physical + buf->tPlanes[PLANE_Y].iOffset;

  /*           <------------> stride
   *           <--------> width
   * height   ^
   *          |
   *          |
   *          v         x last pixel of the image
   * end = (height - 1) * stride + width - 1 (to get the last pixel of the image)
   * total_size = height * stride
   * end = total_size - stride + width - 1
   */
  // TODO : This should be LCU and 64 aligned
  config.luma_end_address[XVSFSYNC_PROD] = config.luma_start_address[XVSFSYNC_PROD] + xvsfsync_chan_getLumaSize (buf) - buf->tPlanes[PLANE_Y].iPitch + buf->tDim.iWidth - 1;
  config.luma_start_address[XVSFSYNC_CONS] = physical + buf->tPlanes[PLANE_Y].iOffset;
  config.luma_end_address[XVSFSYNC_CONS] = config.luma_start_address[XVSFSYNC_CONS] + xvsfsync_chan_getLumaSize (buf) - buf->tPlanes[PLANE_Y].iPitch + buf->tDim.iWidth - 1;

  /* chroma is the same, but the width depends on the format of the yuv
   * here we make the assumption that the fourcc is semi planar */
  if(!IsMonochrome(buf->tFourCC))
  {
    assert(IsSemiPlanar(buf->tFourCC));
    config.chroma_start_address[XVSFSYNC_PROD] = physical + xvsfsync_chan_getOffsetUV (buf);
    // TODO : This should be LCU and 64 aligned
    config.chroma_end_address[XVSFSYNC_PROD] = config.chroma_start_address[XVSFSYNC_PROD] + xvsfsync_chan_getChromaSize (buf) - buf->tPlanes[PLANE_UV].iPitch + buf->tDim.iWidth - 1;
    config.chroma_start_address[XVSFSYNC_CONS] = physical + xvsfsync_chan_getOffsetUV (buf);
    config.chroma_end_address[XVSFSYNC_CONS] = config.chroma_start_address[XVSFSYNC_CONS] + xvsfsync_chan_getChromaSize (buf) - buf->tPlanes[PLANE_UV].iPitch + buf->tDim.iWidth - 1;
  }
  else
  {
    for(int user = 0; user < XVSFSYNC_IO; user++)
    {
      config.chroma_start_address[user] = 0;
      config.chroma_end_address[user] = 0;
      config.ismono[user] = 1;
    }
  }

  for(int core = 0; core < XVSFSYNC_MAX_CORES; core++)
  {
    config.luma_core_offset[core] = 0;
    config.chroma_core_offset[core] = 0;
  }

  /* no margin for now (only needed for the decoder) */
  config.luma_margin = 0;
  config.chroma_margin = 0;

  config.fb_id[XVSFSYNC_PROD] = XVSFSYNC_AUTO_SEARCH;
  config.fb_id[XVSFSYNC_CONS] = XVSFSYNC_AUTO_SEARCH;
  config.channel_id = channelId;

  return config;
}

static void xvfbsync_syncChan_listener (struct ChannelStatus1* status)
{
  printf ("watchdog: %d, sync: %d, ldiff: %d, cdiff: %d\n", status->watchdogError, status->syncError, status->lumaDiffError, status->chromaDiffError);
}

static void xvfbsync_syncChan_disable (struct SyncChannel1* syncChan)
{
  if (!syncChan->enabled)
    assert (0 == "Tried to disable a channel twice");

  xvfbsync_syncIP_disableChannel (syncChan->sync, syncChan->id);
  syncChan->enabled = false;
  printf ("Disable channel %d\n", syncChan->id);
}

/* ***************** */
/* xvfbsync syncChan */
/* ***************** */

static void xvfbsync_syncChan_populate (struct SyncChannel1* syncChan, struct SyncIp1* syncIP, int id)
{
  syncChan->sync = syncIP;
  syncChan->id = id;
  syncChan->enabled = false;
  xvfbsync_syncIP_addListener(syncIP, id, &xvfbsync_syncChan_listener);
}

static void xvfbsync_syncChan_depopulate (struct SyncChannel1* syncChan)
{
  if(syncChan->enabled)
    xvfbsync_syncChan_disable (syncChan);

  xvfbsync_syncIP_removeListener(syncChan->sync, syncChan->id);
}

/* ******************** */
/* xvfbsync decSyncChan */
/* ******************** */

void xvfbsync_decSyncChan_addBuffer(struct DecSyncChannel1* decSyncChan, LLP2Buf* buf)
{
  struct xvsfsync_chan_config config = setDecFrameBufferConfig(decSyncChan->syncChannel.id, buf);
  //printFrameBufferConfig(config, decSyncChan->syncChannel->sync->maxUsers, decSyncChan->syncChannel->sync->maxCores);

  xvfbsync_syncIP_addBuffer(decSyncChan->syncChannel.sync, &config);
  printf ("Pushed buffer in sync ip\n");
  //printChannelStatus(sync->getStatus(id));
}

void xvfbsync_decSyncChan_enable(struct DecSyncChannel1* decSyncChan)
{
  xvfbsync_syncIP_enableChannel (decSyncChan->syncChannel.sync, decSyncChan->syncChannel.id);
  decSyncChan->syncChannel.enabled = true;
}

void xvfbsync_decSyncChan_populate(struct DecSyncChannel1* decSyncChan, struct SyncIp1* syncIP, int id)
{
  xvfbsync_syncChan_populate (&(decSyncChan->syncChannel), syncIP, id);
}

void xvfbsync_decSyncChan_depopulate(struct DecSyncChannel1* decSyncChan)
{
  xvfbsync_syncChan_depopulate (&(decSyncChan->syncChannel));
}

/* **************************** */
/* xvfbsync encSyncChan helpers */
/* **************************** */

static void xvfbsync_encSyncChan_addBuffer_(struct EncSyncChannel1* encSyncChan, LLP2Buf* buf, int numFbToEnable)
{
  if (buf)
  {
    /* we do not support adding buffer when the pipeline is running */
    assert (!encSyncChan->isRunning);
    xvfbsync_queue_push (&encSyncChan->buffers, buf);
  }

  /* If we don't want to start the ip yet, we do not program
   * the ip registers, we just keep the buffer in our queue
   *
   * When we start the channel, we try to add as much buffer
   * as we can inside the hw ip.
   *
   * Once we are running, we keep the same set of buffer and when
   * one of the buffer is finished we replace it with a new one from the queue
   * in a round robin fashion */

  while(encSyncChan->isRunning && numFbToEnable > 0 && !xvfbsync_queue_empty (&encSyncChan->buffers))
  {
    buf = xvfbsync_queue_front (&encSyncChan->buffers);

    struct xvsfsync_chan_config config = setEncFrameBufferConfig(encSyncChan->syncChannel.id, buf, encSyncChan->hardwareHorizontalStrideAlignment, encSyncChan->hardwareVerticalStrideAlignment);
    //printFrameBufferConfig(config, sync->maxUsers, sync->maxCores);

    xvfbsync_syncIP_addBuffer(encSyncChan->syncChannel.sync, &config);
    printf ("Pushed buffer in sync ip\n");
    //printChannelStatus(sync->getStatus(id));
    xvfbsync_queue_pop (&encSyncChan->buffers);

    xvfbsync_queue_push (&encSyncChan->buffers, buf);
    --numFbToEnable;
  }
}

/* ******************** */
/* xvfbsync encSyncChan */
/* ******************** */

void xvfbsync_encSyncChan_addBuffer(struct EncSyncChannel1* encSyncChan, LLP2Buf* buf)
{
  pthread_mutex_lock (&encSyncChan->mutex);  
  xvfbsync_encSyncChan_addBuffer_ (encSyncChan, buf, 1);
  pthread_mutex_unlock (&encSyncChan->mutex);
}

void xvfbsync_encSyncChan_enable(struct EncSyncChannel1* encSyncChan)
{
  pthread_mutex_lock (&encSyncChan->mutex);
  encSyncChan->isRunning = true;
  int numFbToEnable = MIN((int)encSyncChan->buffers.size, encSyncChan->syncChannel.sync->maxBuffers);
  xvfbsync_encSyncChan_addBuffer_ (encSyncChan, NULL, numFbToEnable);
  xvfbsync_syncIP_enableChannel (encSyncChan->syncChannel.sync, encSyncChan->syncChannel.id);
  encSyncChan->syncChannel.enabled = true;
  printf ("Enable channel %d\n", encSyncChan->syncChannel.id);
  pthread_mutex_unlock (&encSyncChan->mutex);
}

void xvfbsync_encSyncChan_populate (struct EncSyncChannel1* encSyncChan, struct SyncIp1* syncIP, int id, int hardwareHorizontalStrideAlignment, int hardwareVerticalStrideAlignment)
{
  xvfbsync_syncChan_populate (&(encSyncChan->syncChannel), syncIP, id);
  encSyncChan->isRunning = false;
  encSyncChan->hardwareHorizontalStrideAlignment = hardwareHorizontalStrideAlignment;
  encSyncChan->hardwareVerticalStrideAlignment = hardwareVerticalStrideAlignment;
  if (pthread_mutex_init (&(encSyncChan->mutex), NULL)) {
    printf ("Couldn't intialize lock");
    return;
  }
  xvfbsync_queue_init (&(encSyncChan->buffers));  
}

void xvfbsync_encSyncChan_depopulate (struct EncSyncChannel1* encSyncChan)
{
  xvfbsync_syncChan_depopulate (&encSyncChan->syncChannel);

  while (!xvfbsync_queue_empty (&encSyncChan->buffers))
  {
    LLP2Buf* buf = xvfbsync_queue_front (&encSyncChan->buffers);
    free(buf);
    xvfbsync_queue_pop (&encSyncChan->buffers);
  }
}
