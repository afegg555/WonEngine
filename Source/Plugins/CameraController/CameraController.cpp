#include "CameraController.h"
#include "Backlog.h"

namespace won::plugin
{
    class CameraController : public IPlugin
    {
        struct ControllerStateInternal
        {
            ControllerState state;

        };
        struct CameraStateInternal
        {
            CameraState cam_state;
            float2 mouse_start;
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

        void PanMove(const float2& mouse_delta, CameraState& cam_state)
        {
            const float dx = mouse_delta.x / internal_controller_state.state.screen_size.x;
            const float dy = -mouse_delta.y / internal_controller_state.state.screen_size.y;

            XMVECTOR cam_pos = XMLoadFloat3(&cam_state.cam_pos);
            XMVECTOR cam_view = XMVector3Normalize(XMLoadFloat3(&cam_state.cam_view));
            XMVECTOR cam_up = XMVector3Normalize(XMLoadFloat3(&cam_state.cam_up));

            XMVECTOR cam_right = XMVector3Normalize(XMVector3Cross(cam_up, cam_view));
            XMVECTOR cam_up_ortho = XMVector3Cross(cam_view, cam_right);

            XMVECTOR basis_x = cam_right;
            XMVECTOR basis_y = cam_up_ortho;
            XMVECTOR focus_point = XMLoadFloat3(&internal_controller_state.state.focus_point);
            float dist = XMVectorGetX(XMVector3Length(cam_pos - focus_point));

            XMVECTOR delta =
                XMVectorAdd(
                    XMVectorScale(basis_x, -dx * dist),
                    XMVectorScale(basis_y, -dy * dist)
                );

            cam_pos = XMVectorAdd(cam_pos, delta);

            XMStoreFloat3(&cam_state.cam_pos, cam_pos);
        }

        void Orbit(const float2& mouse_delta, CameraState& cam_state)
        {
            const float dx = mouse_delta.x / internal_controller_state.state.screen_size.x;
            const float dy = mouse_delta.y / internal_controller_state.state.screen_size.y;

            const float yaw = dx * XM_2PI * internal_controller_state.state.rotate_speed;
            const float pitch = dy * XM_2PI * internal_controller_state.state.rotate_speed;

            XMVECTOR cam_pos = XMLoadFloat3(&cam_state.cam_pos);
            XMVECTOR cam_view = XMVector3Normalize(XMLoadFloat3(&cam_state.cam_view));
            XMVECTOR cam_up = XMVector3Normalize(XMLoadFloat3(&cam_state.cam_up));

            XMVECTOR cam_right = XMVector3Normalize(XMVector3Cross(cam_up, cam_view));
            XMVECTOR focus_point = XMLoadFloat3(&internal_controller_state.state.focus_point);

            XMVECTOR world_up = XMVectorSet(0, 1, 0, 0);
            XMMATRIX yaw_m = XMMatrixRotationAxis(world_up, yaw);

            XMVECTOR offset = XMVectorSubtract(cam_pos, focus_point);
            offset = XMVector3TransformCoord(offset, yaw_m);
            cam_pos = XMVectorAdd(focus_point, offset);

            cam_view = XMVector3Normalize(XMVector3Transform(cam_view, yaw_m));
            cam_up = XMVector3Normalize(XMVector3Transform(cam_up, yaw_m));
            cam_right = XMVector3Normalize(XMVector3Cross(cam_up, cam_view));

            XMMATRIX pitch_m = XMMatrixRotationAxis(cam_right, pitch);

            offset = XMVectorSubtract(cam_pos, focus_point);
            offset = XMVector3TransformCoord(offset, pitch_m);
            cam_pos = XMVectorAdd(focus_point, offset);

            cam_view = XMVector3Normalize(XMVector3Transform(cam_view, pitch_m));
            cam_up = XMVector3Normalize(XMVector3Transform(cam_up, pitch_m));

            XMStoreFloat3(&cam_state.cam_pos, cam_pos);
            XMStoreFloat3(&cam_state.cam_view, cam_view);
            XMStoreFloat3(&cam_state.cam_up, cam_up);
        }

        void Rotate(const float2& mouse_delta, CameraState& cam_state)
        {
            const float dx = mouse_delta.x / internal_controller_state.state.screen_size.x;
            const float dy = mouse_delta.y / internal_controller_state.state.screen_size.y;

            const float yaw = dx * XM_2PI * internal_controller_state.state.rotate_speed;
            float pitch = dy * XM_2PI * internal_controller_state.state.rotate_speed;

            XMVECTOR cam_view = XMVector3Normalize(XMLoadFloat3(&cam_state.cam_view));
            XMVECTOR cam_up = XMVector3Normalize(XMLoadFloat3(&cam_state.cam_up));
            const XMVECTOR world_up = XMVectorSet(0.f, 1.f, 0.f, 0.f);

            const float current_pitch = std::asin(math::clamp(XMVectorGetY(cam_view), -1.f, 1.f));
            const float next_pitch = math::clamp(current_pitch + pitch, -XM_PIDIV2 + 0.001f, XM_PIDIV2 - 0.001f);
            pitch = next_pitch - current_pitch;

            const XMMATRIX yaw_matrix = XMMatrixRotationAxis(world_up, yaw);
            cam_view = XMVector3Normalize(XMVector3TransformNormal(cam_view, yaw_matrix));
            cam_up = XMVector3Normalize(XMVector3TransformNormal(cam_up, yaw_matrix));

            const XMVECTOR cam_right = XMVector3Normalize(XMVector3Cross(cam_up, cam_view));
            const XMMATRIX pitch_matrix = XMMatrixRotationAxis(cam_right, pitch);
            cam_view = XMVector3Normalize(XMVector3TransformNormal(cam_view, pitch_matrix));
            cam_up = XMVector3Normalize(XMVector3Cross(cam_view, cam_right));

            XMStoreFloat3(&cam_state.cam_view, cam_view);
            XMStoreFloat3(&cam_state.cam_up, cam_up);
        }
    private:
        static void SetControllerStateThunk(IPlugin* self, const ControllerState& in_controller_state)
        {
            return static_cast<CameraController*>(self)->SetControllerState(in_controller_state);
        }
        static void PanMoveThunk(IPlugin* self, const XMFLOAT2& mouse_delta, CameraState& cam_state)
        {
            return static_cast<CameraController*>(self)->PanMove(mouse_delta, cam_state);
        }
        static void OrbitThunk(IPlugin* self, const XMFLOAT2& mouse_delta, CameraState& cam_state)
        {
            return static_cast<CameraController*>(self)->Orbit(mouse_delta, cam_state);
        }
        static void RotateThunk(IPlugin* self, const XMFLOAT2& mouse_delta, CameraState& cam_state)
        {
            return static_cast<CameraController*>(self)->Rotate(mouse_delta, cam_state);
        }

        inline static CameraControllerAPI s_api{
            &SetControllerStateThunk,
            &PanMoveThunk,
            &RotateThunk,
            &OrbitThunk
        };

        ControllerStateInternal internal_controller_state;
    };

    IMPLEMENT_PLUGIN(CameraController, PCameraController);
}
