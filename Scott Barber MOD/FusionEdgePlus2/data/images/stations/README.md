# Creating custom station icons

Station icons are stored in `data/images/stations`.

1. Create a standard RGB or RGBA PNG image for the station. The recommended
   size is exactly **80 x 80 pixels**; 24-bit RGB is the safest format. A black
   background usually blends best with the default FusionEdge layout.
2. Give the file a short, unique name, for example `polskieradio.png`, and copy
   it into `data/images/stations`.
3. Add a line to `map.csv` using this format:

   ```text
   Station name<TAB>polskieradio.png
   ```

   Insert a real tab character between the station name and the filename, not
   spaces or a comma. The station name should match the name used in
   `playlist.csv`.
4. Upload the complete `images` folder through **WebUI > Options > Board >
   images folder**, or upload the LittleFS filesystem normally.
5. Restart the radio if the previous icon map has already been loaded.

Several station names may point to the same PNG file. If no matching entry is
found, FusionEdge displays the default WEB mode icon. The map supports up to 80
station-name entries.
