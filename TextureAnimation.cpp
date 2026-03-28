#include "TextureAnimation.h"
#include <stdexcept>
#include <cmath>

void TextureAnimator::Register(const std::string& name, const UVAnimParams& params)
{
    mParams[name] = params;
}

void TextureAnimator::Update(float dt)
{
    for (auto& pair : mParams)
    {
        UVAnimParams& p = pair.second;

        p.Offset.x = fmodf(p.Offset.x + p.ScrollSpeed.x * dt, 1.0f);
        p.Offset.y = fmodf(p.Offset.y + p.ScrollSpeed.y * dt, 1.0f);

        p.RotateAngle = fmodf(p.RotateAngle + p.RotateSpeed * dt, 6.28318530f);
    }
}

UVAnimCB TextureAnimator::GetCBData(const std::string& name) const
{
    auto it = mParams.find(name);
    if (it == mParams.end())
        throw std::runtime_error("TextureAnimator: unknown object " + name);

    const UVAnimParams& p = it->second;
    UVAnimCB cb = {};
    cb.Tiling      = p.Tiling;
    cb.Offset      = p.Offset;
    cb.RotateAngle = p.RotateAngle;
    return cb;
}

void TextureAnimator::SetTiling(const std::string& name, float u, float v)
{
    mParams[name].Tiling = { u, v };
}

void TextureAnimator::SetScrollSpeed(const std::string& name, float u, float v)
{
    mParams[name].ScrollSpeed = { u, v };
}
