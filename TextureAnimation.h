#pragma once
#include <DirectXMath.h>
#include <string>
#include <unordered_map>

using namespace DirectX;

// ─────────────────────────────────────────────────────────────────────────────
// Параметры UV-анимации одного объекта
// ─────────────────────────────────────────────────────────────────────────────
struct UVAnimParams
{
    // Тайлинг (масштаб UV): (2,2) = текстура повторяется 2 раза по U и V
    XMFLOAT2 Tiling     = { 1.0f, 1.0f };

    // Скорость скролла (единиц UV в секунду)
    XMFLOAT2 ScrollSpeed = { 0.0f, 0.0f };

    // Текущее смещение (обновляется каждый кадр)
    XMFLOAT2 Offset     = { 0.0f, 0.0f };

    // Вращение UV (радиан в секунду)
    float RotateSpeed   = 0.0f;
    float RotateAngle   = 0.0f;   // текущий угол
};

// ─────────────────────────────────────────────────────────────────────────────
// Constant buffer структура (должна совпадать с HLSL cbuffer)
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(256) UVAnimCB     // 256-байт выравнивание обязательно для DX12
{
    XMFLOAT2 Tiling;
    XMFLOAT2 Offset;
    float    RotateAngle;
    float    Pad0[3];
};

// ─────────────────────────────────────────────────────────────────────────────
// TextureAnimator — обновляет UV-параметры, отдаёт готовый CB на заливку
// ─────────────────────────────────────────────────────────────────────────────
class TextureAnimator
{
public:
    // Регистрирует объект с настройками анимации
    void Register(const std::string& objectName, const UVAnimParams& params);

    // Обновить все анимации (вызывать каждый кадр)
    void Update(float deltaTime);

    // Получить данные для заливки в constant buffer
    UVAnimCB GetCBData(const std::string& objectName) const;

    // Установить тайлинг вручную (например, из UI)
    void SetTiling(const std::string& objectName, float u, float v);

    // Установить скорость скролла
    void SetScrollSpeed(const std::string& objectName, float u, float v);

private:
    std::unordered_map<std::string, UVAnimParams> mParams;
};
