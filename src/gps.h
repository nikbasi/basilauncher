#pragma once

#include <stdint.h>

struct GpsSat {
  int el = -1;
  int az = 0;
  int snr = 0;
};

struct GpsView {
  int fix = 0;  // 0 searching, 2 = 2D, 3 = 3D
  int satsUsed = 0;
  int satsView = 0;
  bool hasPos = false;
  double lat = 0;
  double lon = 0;
  bool hasAlt = false;
  int altM = 0;
  int zoom = 15;
  int zoomMin = 13;
  int zoomMax = 16;
  int trackCount = 0;
};

struct GpsPoll {
  bool sentence = false;
  bool fixChanged = false;
};

// fresh clears the walked path. Resuming from the shade passes false.
void gpsStart(bool fresh);
// Radio off. The walked path stays in memory.
void gpsPause();
// Radio off, then write /gps/track.gpx if this session has points.
void gpsLeave();
bool gpsActive();
// True from gpsStart until gpsLeave. Opening the shade pauses the radio and
// does not end the session.
bool gpsSession();

GpsPoll gpsPoll();
const GpsView& gpsView();
int gpsSatCount();
GpsSat gpsSat(int index);

// True when the level changed and a tile exists at the map center.
bool gpsZoomIn();
bool gpsZoomOut();
// Zoom around a point measured from the map viewport center. Pinch scale <1
// zooms out and >1 zooms in while keeping that map point under the fingers.
bool gpsPinchZoom(float scale, int offsetX, int offsetY);
// Finger movement in screen pixels. The map follows the finger.
bool gpsPan(int dx, int dy);
// Slide the map already drawn by this many screen pixels. Exposed edges come
// from tiles kept in RAM. No card access. False when the pane is not ready.
bool gpsScrollMap(int dx, int dy, int x, int y, int w, int h);
// True when follow mode would slide the map onto a newer fix.
bool gpsMapFollowMoved();
// Put the map back on the fix. False when there is no position yet.
bool gpsRecenter();

// Draw tiles, the walked path, the crosshair, and the scale bar.
// The pane stays browsable before a fix. False when no tile covers the view.
bool gpsDrawMap(int x, int y, int w, int h);
// True after a card read found /maps. False until then, including a missing card.
bool gpsHasMaps();
