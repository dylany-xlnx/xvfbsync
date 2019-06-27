#include <sys/ioctl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <pthread.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

#include "xvsfsync.h"

#define BIT(x) (1 << (x))
#define MAX_FB_NUMBER 3
#define MAX_USER 2 /* consumer and producter */

#define XVFBSYNC_FOURCC2(A, B, C, D) ((uint32_t)(((uint32_t)((A))) \
                                       | ((uint32_t)((B)) << 8) \
                                       | ((uint32_t)((C)) << 16) \
                                       | ((uint32_t)((D)) << 24)))

#define FOURCC_MAPPING(FCC, ChromaMode, BD, StorageMode, ChromaOrder, Compression, Packed10) { FCC, { ChromaMode, BD, StorageMode, ChromaOrder, Compression, Packed10 } \
}

typedef enum e_ChromaMode1
{
  CHROMA_MONO, /*!< Monochrome */
  CHROMA_4_0_0 = CHROMA_MONO, /*!< 4:0:0 = Monochrome */
  CHROMA_4_2_0, /*!< 4:2:0 chroma sampling */
  CHROMA_4_2_2, /*!< 4:2:2 chroma sampling */
  CHROMA_4_4_4, /*!< 4:4:4 chroma sampling : Not supported */
  CHROMA_MAX_ENUM, /* sentinel */
} EChromaMode;

typedef enum e_FbStorageMode1
{
  FB_RASTER = 0,
  FB_TILE_32x4 = 2,
  FB_TILE_64x4 = 3,
  FB_MAX_ENUM, /* sentinel */
} EFbStorageMode;

typedef enum e_ChromaOrder1
{
  C_ORDER_NO_CHROMA,
  C_ORDER_U_V,
  C_ORDER_V_U,
  C_ORDER_SEMIPLANAR
} EChromaOrder;

typedef struct t_PicFormat1
{
  EChromaMode eChromaMode;
  uint8_t uBitDepth;
  EFbStorageMode eStorageMode;
  EChromaOrder eChromaOrder;
  bool bCompressed;
  bool b10bPacked;
} TPicFormat;

typedef struct tFourCCMapping1
{
  uint32_t tfourCC;
  TPicFormat tPictFormat;
} TFourCCMapping;

typedef enum e_PlaneId1
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
