#include "CameraController.h"
#include "Backlog.h"

#include <cmath>

namespace won::plugin
{
    namespace
    {
        constexpr float pitch_limit = XM_PIDIV2 - 0.001f;

        XMVECTOR BaseForward()
        {
            return XMVectorSet(0.f, 0.f, 1.f, 0.f);
        }

        XMVECTOR BaseUp()
        {
            return XMVectorSet(0.f, 1.f, 0.f, 0.f);
        }

        XMVECTOR BaseRight()
        {
            return XMVectorSet(1.f, 0.f, 0.f, 0.f);
        }
    }

    class CameraController : public IPlugin
    {
        struct ControllerStateInternal
        {
            ControllerState state;
            CameraInteractionMode interaction_mode = CameraInteractionMode::None;
            float yaw = 0.f;
            float pitch = 0.f;
            float orbit_distance = 0.f;
        };

    public:
        virtual const char* GetName() const override { return WON_IID_CAMERA_CONTROLLER; }
        virtual const char* GetVersion() const override { return WON_VID_CAMERA_CONTROLLER; }

        virtual void* QueryInterface(const char* iid, const char* version_id) const override
        {
            if (std::strcmp(iid, WON_IID_CAMERA_CONTROLLER) == 0 && std::strcmp(version_id, WON_VID_CAMERA_CONTROLLER) == 0)
                return (void*)&s_api;
            return nullptr;
        }
        virtual bool Initialize() override
        {
            // add something to initialize on LoadPlugin
            return true;
        }
        virtual void Shutdown() override
        {
            // add something to shutdown on UnloadPlugin
        }

        void SetControllerState(const ControllerState& in_controller_state)
        {
            internal_controller_state.state = in_controller_state;
        }

        void BeginInteraction(CameraInteractionMode mode, const CameraState& cam_state)
        {
            internal_controller_state.interaction_mode = mode;

            XMVECTOR cam_rotation = XMQuaternionNormalize(XMLoadFloat4(&cam_state.cam_rotation));
            XMVECTOR cam_forward = XMVector3Rotate(BaseForward(), cam_rotation);
            XMVECTOR cam_right = XMVector3Rotate(BaseRight(), cam_rotation);

            internal_controller_state.pitch = std::asin(math::clamp(XMVectorGetY(cam_forward), -1.f, 1.f));
            const float cos_pitch = std::cos(internal_controller_state.pitch);
            if (std::abs(cos_pitch) > 0.0001f)
            {
                internal_controller_state.yaw = std::atan2(XMVectorGetX(cam_forward), XMVectorGetZ(cam_forward));
            }
            else
            {
                internal_controller_state.yaw = std::atan2(-XMVectorGetZ(cam_right), XMVectorGetX(cam_right));
            }

            XMVECTOR cam_pos = XMLoadFloat3(&cam_state.cam_pos);
            XMVECTOR focus_point = XMLoadFloat3(&internal_controller_state.state.focus_point);
            internal_controller_state.orbit_distance = XMVectorGetX(XMVector3Length(cam_pos - focus_point));
        }

        void EndInteraction()
        {
            internal_controller_state.interaction_mode = CameraInteractionMode::None;
        }

        void UpdateInteraction(const float2& mouse_delta, CameraState& cam_state)
        {
            switch (internal_controller_state.interaction_mode)
            {
            case CameraInteractionMode::PanMove:
                PanMove(mouse_delta, cam_state);
                break;
            case CameraInteractionMode::Rotate:
                Rotate(mouse_delta, cam_state);
                break;
            case CameraInteractionMode::Orbit:
                Orbit(mouse_delta, cam_state);
                break;
            default:
                break;
            }
        }

    private:
        void PanMove(const float2& mouse_delta, CameraState& cam_state)
        {
            const float dx = mouse_delta.x / internal_controller_state.state.screen_size.x;
            const float dy = -mouse_delta.y / internal_controller_state.state.screen_size.y;

            XMVECTOR cam_pos = XMLoadFloat3(&cam_state.cam_pos);
            XMVECTOR cam_rotation = XMQuaternionNormalize(XMLoadFloat4(&cam_state.cam_rotation));
            XMVECTOR cam_right = XMVector3Rotate(BaseRight(), cam_rotation);
            XMVECTOR cam_up = XMVector3Rotate(BaseUp(), cam_rotation);

            XMVECTOR focus_point = XMLoadFloat3(&internal_controller_state.state.focus_point);
            float dist = XMVectorGetX(XMVector3Length(cam_pos - focus_point));

            XMVECTOR delta =
                XMVectorAdd(
                    XMVectorScale(cam_right, -dx * dist),
                    XMVectorScale(cam_up, -dy * dist)
                );

            cam_pos = XMVectorAdd(cam_pos, delta);

            XMStoreFloat3(&cam_state.cam_pos, cam_pos);
        }

        void Orbit(const float2& mouse_delta, CameraState& cam_state)
        {
            const float dx = mouse_delta.x / internal_controller_state.state.screen_size.x;
            const float dy = -mouse_delta.y / internal_controller_state.state.screen_size.y;

            internal_controller_state.yaw += dx * XM_2PI * internal_controller_state.state.orbit_speed;
            internal_controller_state.pitch = math::clamp(
                internal_controller_state.pitch + dy * XM_2PI * internal_controller_state.state.orbit_speed,
                -pitch_limit,
                pitch_limit);

            const float cos_pitch = std::cos(internal_controller_state.pitch);
            XMVECTOR cam_forward = XMVectorSet(
                std::sin(internal_controller_state.yaw) * cos_pitch,
                std::sin(internal_controller_state.pitch),
                std::cos(internal_controller_state.yaw) * cos_pitch,
                0.f);
            XMVECTOR cam_right = XMVectorSet(
                std::cos(internal_controller_state.yaw),
                0.f,
                -std::sin(internal_controller_state.yaw),
                0.f);
            XMVECTOR cam_up = XMVector3Normalize(XMVector3Cross(cam_forward, cam_right));

            XMMATRIX cam_world = XMMatrixIdentity();
            cam_world.r[0] = XMVectorSetW(cam_right, 0.f);
            cam_world.r[1] = XMVectorSetW(cam_up, 0.f);
            cam_world.r[2] = XMVectorSetW(cam_forward, 0.f);

            XMVECTOR cam_rotation = XMQuaternionNormalize(XMQuaternionRotationMatrix(cam_world));
            XMStoreFloat4(&cam_state.cam_rotation, cam_rotation);

            XMVECTOR focus_point = XMLoadFloat3(&internal_controller_state.state.focus_point);
            XMVECTOR cam_pos = focus_point - XMVectorScale(cam_forward, (std::max)(internal_controller_state.orbit_distance, 0.001f));
            XMStoreFloat3(&cam_state.cam_pos, cam_pos);
        }

        void Rotate(const float2& mouse_delta, CameraState& cam_state)
        {
            const float dx = mouse_delta.x / internal_controller_state.state.screen_size.x;
            const float dy = -mouse_delta.y / internal_controller_state.state.screen_size.y;

            internal_controller_state.yaw += dx * XM_2PI * internal_controller_state.state.rotate_speed;
            internal_controller_state.pitch = math::clamp(
                internal_controller_state.pitch + dy * XM_2PI * internal_controller_state.state.rotate_speed,
                -pitch_limit,
                pitch_limit);

            const float cos_pitch = std::cos(internal_controller_state.pitch);
            XMVECTOR cam_forward = XMVectorSet(
                std::sin(internal_controller_state.yaw) * cos_pitch,
                std::sin(internal_controller_state.pitch),
                std::cos(internal_controller_state.yaw) * cos_pitch,
                0.f);
            XMVECTOR cam_right = XMVectorSet(
                std::cos(internal_controller_state.yaw),
                0.f,
                -std::sin(internal_controller_state.yaw),
                0.f);
            XMVECTOR cam_up = XMVector3Normalize(XMVector3Cross(cam_forward, cam_right));

            XMMATRIX cam_world = XMMatrixIdentity();
            cam_world.r[0] = XMVectorSetW(cam_right, 0.f);
            cam_world.r[1] = XMVectorSetW(cam_up, 0.f);
            cam_world.r[2] = XMVectorSetW(cam_forward, 0.f);

            XMVECTOR cam_rotation = XMQuaternionNormalize(XMQuaternionRotationMatrix(cam_world));
            XMStoreFloat4(&cam_state.cam_rotation, cam_rotation);
        }

        static void SetControllerStateThunk(IPlugin* self, const ControllerState& in_controller_state)
        {
            return static_cast<CameraController*>(self)->SetControllerState(in_controller_state);
        }
        static void BeginInteractionThunk(IPlugin* self, CameraInteractionMode mode, const CameraState& cam_state)
        {
            return static_cast<CameraController*>(self)->BeginInteraction(mode, cam_state);
        }
        static void UpdateInteractionThunk(IPlugin* self, const XMFLOAT2& mouse_delta, CameraState& cam_state)
        {
            return static_cast<CameraController*>(self)->UpdateInteraction(mouse_delta, cam_state);
        }
        static void EndInteractionThunk(IPlugin* self)
        {
            return static_cast<CameraController*>(self)->EndInteraction();
        }

        inline static CameraControllerAPI s_api{
            &SetControllerStateThunk,
            &BeginInteractionThunk,
            &UpdateInteractionThunk,
            &EndInteractionThunk,
        };

        ControllerStateInternal internal_controller_state;
    };

    IMPLEMENT_PLUGIN(CameraController, PCameraController);
}
