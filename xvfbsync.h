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

struct ThreadInfo
{
  struct SyncIp1* syncIP;
};


int xvfbsync_syncIP_getFreeChannel(struct SyncIp1* syncIP);
int xvfbsync_syncIP_populate (struct SyncIp1* syncIP, int fd);
void xvfbsync_syncIP_depopulate (struct SyncIp1* syncIP);

void xvfbsync_decSyncChan_addBuffer(struct DecSyncChannel1* decSyncChan, LLP2Buf* buf);
void xvfbsync_decSyncChan_enable(struct DecSyncChannel1* decSyncChan);
void xvfbsync_decSyncChan_populate(struct DecSyncChannel1* decSyncChan, struct SyncIp1* syncIP, int id);
void xvfbsync_decSyncChan_depopulate(struct DecSyncChannel1* decSyncChan);

void xvfbsync_encSyncChan_addBuffer(struct EncSyncChannel1* encSyncChan, LLP2Buf* buf);
void xvfbsync_encSyncChan_enable(struct EncSyncChannel1* encSyncChan);
void xvfbsync_encSyncChan_populate (struct EncSyncChannel1* encSyncChan, struct SyncIp1* syncIP, int id, int hardwareHorizontalStrideAlignment, int hardwareVerticalStrideAlignment);
void xvfbsync_encSyncChan_depopulate (struct EncSyncChannel1* encSyncChan);
