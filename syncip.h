#include <sys/ioctl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <pthread.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

#include "xvsfsync.h"
#include "lib_common/IDriver.h"
#include "lib_common/BufferAPI.h"
#include "lib_common/BufferSrcMeta.h"
#include "lib_common/FourCC.h"

#define BIT(x) (1 << (x))
#define MAX_FB_NUMBER 3
#define MAX_USER 2 /* consumer and producter */

typedef enum e_PlaneId
{
  PLANE_Y,
  PLANE_UV,
  PLANE_MAP_Y,
  PLANE_MAP_UV,
  PLANE_MAX_ENUM, /* sentinel */
} EPlaneId;

struct TDimension
{
  int32_t iWidth;
  int32_t iHeight;
};

struct TPlane
{
  int iOffset; /*!< Offset of the plane from beginning of the buffer (in bytes) */
  int iPitch; /*!< Pitch of the plane (in bytes) */
};

typedef struct LLP2Buf_
{
  uint32_t phyAddr; /* 32-bit addr reg */
  uint32_t tFourCC; /* Color format */
  struct TDimension tDim; /* Dimension in pixel of the frame */
  struct TPlane tPlanes[PLANE_MAX_ENUM]; /* Array of color planes parameters  */
} LLP2Buf;

struct ChannelStatus1
{
  bool fbAvail[MAX_FB_NUMBER][MAX_USER];
  bool enable;
  bool syncError;
  bool watchdogError;
  bool lumaDiffError;
  bool chromaDiffError;
};

struct Node
{
  LLP2Buf* buf;
  struct Node* next;
};

struct Queue
{
  struct Node* front;
  struct Node* last;
  unsigned int size;
};

struct SyncIp1
{
  int maxChannels;
  int maxUsers;
  int maxBuffers;
  int maxCores;
  int fd;
  bool quit;
  pthread_t pollingThread;
  AL_TDriver* driver;
  pthread_mutex_t mutex;
  void (*(*eventListeners)) (struct ChannelStatus1*); 
  struct ChannelStatus1* channelStatuses;
};

struct SyncChannel1
{
  int id;
  bool enabled;
  struct SyncIp1* sync;
};

struct EncSyncChannel1
{
  struct SyncChannel1 syncChannel;
  struct Queue buffers;
  pthread_mutex_t mutex;
  bool isRunning;
  int hardwareHorizontalStrideAlignment;
  int hardwareVerticalStrideAlignment;
};

struct DecSyncChannel1
{
  struct SyncChannel1 syncChannel;
};

struct threadInfo
{
  struct SyncIp1* syncIP;
};


void xvfbsync_syncIP_getLatestChanStatus(struct SyncIp1* syncIP);
void xvfbsync_syncIP_resetStatus(struct SyncIp1* syncIP, int chanId);
int xvfbsync_syncIP_getFreeChannel(struct SyncIp1* syncIP);
void xvfbsync_syncIP_enableChannel(struct SyncIp1* syncIP, int chanId);
void xvfbsync_syncIP_disableChannel(struct SyncIp1* syncIP, int chanId);
void xvfbsync_syncIP_addBuffer(struct SyncIp1* syncIP, struct xvsfsync_chan_config* fbConfig);
void xvfbsync_syncIP_pollErrors(struct SyncIp1* syncIP, int timeout);
static void* xvfbsync_syncIP_pollingRoutine(void* arg);
void xvfbsync_syncIP_addListener(struct SyncIp1* syncIP, int chanId, void (*delegate)(struct ChannelStatus1*));
void xvfbsync_syncIP_removeListener(struct SyncIp1* syncIP, int chanId);
struct ChannelStatus1* xvfbsync_syncIP_getStatus(struct SyncIp1* syncIP, int chanId);
int xvfbsync_syncIP_populate (struct SyncIp1* syncIP, AL_TDriver* driver, char const* device);
void xvfbsync_syncIP_depopulate (struct SyncIp1* syncIP);
void xvfbsync_syncChan_disable (struct SyncChannel1* syncChan);
void xvfbsync_syncChan_populate (struct SyncChannel1* syncChan, struct SyncIp1* syncIP, int id);
void xvfbsync_syncChan_depopulate (struct SyncChannel1* syncChan);
void xvfbsync_decSyncChan_addBuffer(struct DecSyncChannel1* decSyncChan, AL_TBuffer* buf);
void xvfbsync_decSyncChan_enable(struct DecSyncChannel1* decSyncChan);
void xvfbsync_decSyncChan_populate(struct DecSyncChannel1* decSyncChan, struct SyncIp1* syncIP, int id);
void xvfbsync_decSyncChan_depopulate(struct DecSyncChannel1* decSyncChan);
void samplelib();
int addBuffer(struct xvsfsync_chan_config* fbConfig);
