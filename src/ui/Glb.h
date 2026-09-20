// glTF 2.0 / GLB loader (CPU side): pulls the triangle data and the base
// colour texture out of a .glb file. Enough for the cartridge models from
// asset stores (position/normal/uv + one material texture).
#pragma once

#include <string>
#include <vector>

struct SDL_Surface;

namespace ndsui {

class Mesh;

// Loads a .glb (binary glTF). On success fills the mesh and, if the model has
// a base colour texture, returns it as an ARGB8888 SDL_Surface (caller owns).
// Returns false when the file cannot be parsed.
bool loadGlb(const std::string& path, Mesh& mesh, SDL_Surface** outTex,
             bool* outFlipV);

}  // namespace ndsui
