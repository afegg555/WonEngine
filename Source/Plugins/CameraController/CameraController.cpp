#include "CameraController.h"
#include "Backlog.h"

#define ARCBALL 0

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
#if ARCBALL
            float3 arc_start;
#else
            float2 mouse_start;
#endif
        };

#if ARCBALL
        float3 MapSphereCoordinate(const float2& p)
        {
            // view-space arc
            const float w = internal_controller_state.state.screen_size.x;
            const float h = internal_controller_state.state.screen_size.y;
            const float d = std::max(w, h);
            const float r = d * 0.5f;
            const float cx = w * 0.5f;
            const float cy = h * 0.5f;

            float x = (p.x - cx) / r;
            float y = (cy - p.y) / r;
            float z2 = 1.f - x * x - y * y;

            float3 v;
            if (z2 > 0.f)
            {
                v = { x, y, std::sqrt(z2) };
            }
            else
            {
                float len = std::sqrt(x * x + y * y);
                float inv = (len > FLT_EPSILON) ? (1.f / len) : 0.f;
                v = { x * inv, y * inv, 0.f };
            }
            return v;
        }

        XMVECTOR QuatFromUnitVectors(XMVECTOR u, XMVECTOR v)
        {
            float d = XMVectorGetX(XMVector3Dot(u, v));
            d = std::clamp(d, -1.0f, 1.0f);

            if (d > 0.999999f)
                return XMQuaternionIdentity();

            if (d < -0.999999f)
            {
                XMVECTOR axis = XMVector3Cross(u, XMVectorSet(1, 0, 0, 0));
                if (XMVectorGetX(XMVector3LengthSq(axis)) < 1e-6f)
                    axis = XMVector3Cross(u, XMVectorSet(0, 1, 0, 0));
                axis = XMVector3Normalize(axis);
                return XMQuaternionRotationAxis(axis, XM_PI);
            }

            XMVECTOR axis = XMVector3Cross(u, v);
            XMVECTOR q = XMVectorSetW(axis, 1.0f + d);
            return XMQuaternionNormalize(q);
        }
#endif
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

        void Start(const float2& mouse_pos, const CameraState& in_cam_state)
        {
            internal_camera_state.cam_state = in_cam_state;

#if ARCBALL
            internal_camera_state.arc_start = MapSphereCoordinate(mouse_pos);
#else
            internal_camera_state.mouse_start = mouse_pos;
#endif
        }

        void PanMove(const float2& mouse_pos, CameraState& out_cam_state)
        {
#if ARCBALL
            float3 arc_end = MapSphereCoordinate(mouse_pos);
            const float dx = arc_end.x - internal_camera_state.arc_start.x;
            const float dy = arc_end.y - internal_camera_state.arc_start.y;
#else
            const float dx = (mouse_pos.x - internal_camera_state.mouse_start.x) / internal_controller_state.state.screen_size.x;
            const float dy = -(mouse_pos.y - internal_camera_state.mouse_start.y) / internal_controller_state.state.screen_size.y;
#endif

            XMVECTOR cam_pos = XMLoadFloat3(&internal_camera_state.cam_state.cam_pos);
            XMVECTOR cam_view = XMVector3Normalize(XMLoadFloat3(&internal_camera_state.cam_state.cam_view));
            XMVECTOR cam_up = XMVector3Normalize(XMLoadFloat3(&internal_camera_state.cam_state.cam_up));

            XMVECTOR cam_right = XMVector3Normalize(XMVector3Cross(cam_up, cam_view));
            XMVECTOR cam_up_ortho = XMVector3Cross(cam_view, cam_right);

            XMVECTOR basis_x = cam_right;
            XMVECTOR basis_y = cam_up_ortho;

            float dist = XMVectorGetX(XMVector3Length(cam_pos));
            float scale = (dist > FLT_EPSILON) ? dist : 1.0f;

            XMVECTOR delta =
                XMVectorAdd(
                    XMVectorScale(basis_x, -dx * scale),
                    XMVectorScale(basis_y, -dy * scale)
                );

            cam_pos = XMVectorAdd(cam_pos, delta);

            XMStoreFloat3(&out_cam_state.cam_pos, cam_pos);
            out_cam_state.cam_view = internal_camera_state.cam_state.cam_view;
            out_cam_state.cam_up = internal_camera_state.cam_state.cam_up;
        }

        void Rotate(const float2& mouse_pos, CameraState& out_cam_state)
        {
#if ARCBALL
            float3 arc_end = MapSphereCoordinate(mouse_pos);

            XMVECTOR xarc_start = XMVector3Normalize(XMLoadFloat3(&internal_camera_state.arc_start));
            XMVECTOR xarc_end = XMVector3Normalize(XMLoadFloat3(&arc_end));

            // view-space rotation, angle
            XMVECTOR axis_view = XMVector3Cross(xarc_start, xarc_end);
            float sin_theta = XMVectorGetX(XMVector3Length(axis_view));
            float cos_theta = XMVectorGetX(XMVector3Dot(xarc_start, xarc_end));
            cos_theta = std::clamp(cos_theta, -1.0f, 1.0f);

            if (sin_theta < FLT_EPSILON)
            {
                out_cam_state = internal_camera_state.cam_state;
                return;
            }

            float angle = std::atan2(sin_theta, cos_theta) * internal_controller_state.state.rotate_speed;
            axis_view = XMVectorScale(axis_view, 1.0f / sin_theta); // normalize

            XMVECTOR cam_pos = XMLoadFloat3(&internal_camera_state.cam_state.cam_pos);
            XMVECTOR cam_view = XMVector3Normalize(XMLoadFloat3(&internal_camera_state.cam_state.cam_view));
            XMVECTOR cam_up = XMVector3Normalize(XMLoadFloat3(&internal_camera_state.cam_state.cam_up));

            XMVECTOR cam_right = XMVector3Normalize(XMVector3Cross(cam_up, cam_view));
            XMVECTOR cam_up_ortho = XMVector3Cross(cam_view, cam_right);

            float ax = XMVectorGetX(axis_view);
            float ay = XMVectorGetY(axis_view);
            float az = XMVectorGetZ(axis_view);

            // view-space axis to world-space
            XMVECTOR axis_world =
                XMVectorAdd(
                    XMVectorAdd(XMVectorScale(cam_right, ax), XMVectorScale(cam_up_ortho, ay)),
                    XMVectorScale(cam_view, az));

            axis_world = XMVector3Normalize(axis_world);

            XMMATRIX xrot_matrix = XMMatrixRotationAxis(axis_world, angle);

            cam_pos = XMVector3TransformCoord(cam_pos, xrot_matrix);
            cam_view = XMVector3Normalize(XMVector3Transform(cam_view, xrot_matrix));
            cam_up = XMVector3Normalize(XMVector3Transform(cam_up, xrot_matrix));

            XMStoreFloat3(&out_cam_state.cam_pos, cam_pos);
            XMStoreFloat3(&out_cam_state.cam_view, cam_view);
            XMStoreFloat3(&out_cam_state.cam_up, cam_up);
#else
            const float w = internal_controller_state.state.screen_size.x;
            const float h = internal_controller_state.state.screen_size.y;

            const float dx = (mouse_pos.x - internal_camera_state.mouse_start.x) / (w > 1.f ? w : 1.f);
            const float dy = (mouse_pos.y - internal_camera_state.mouse_start.y) / (h > 1.f ? h : 1.f);

            const float yaw = dx * XM_2PI * internal_controller_state.state.rotate_speed;
            const float pitch = dy * XM_2PI * internal_controller_state.state.rotate_speed;

            XMVECTOR cam_pos = XMLoadFloat3(&internal_camera_state.cam_state.cam_pos);
            XMVECTOR cam_view = XMVector3Normalize(XMLoadFloat3(&internal_camera_state.cam_state.cam_view));
            XMVECTOR cam_up = XMVector3Normalize(XMLoadFloat3(&internal_camera_state.cam_state.cam_up));

            XMVECTOR target = XMVectorZero(); // TODO : may be input ?

            XMVECTOR cam_right = XMVector3Normalize(XMVector3Cross(cam_up, cam_view));

            XMVECTOR world_up = XMVectorSet(0, 1, 0, 0);
            XMMATRIX yaw_m = XMMatrixRotationAxis(world_up, yaw);

            XMVECTOR offset = XMVectorSubtract(cam_pos, target);
            offset = XMVector3TransformCoord(offset, yaw_m);
            cam_pos = XMVectorAdd(target, offset);

            cam_view = XMVector3Normalize(XMVector3Transform(cam_view, yaw_m));
            cam_up = XMVector3Normalize(XMVector3Transform(cam_up, yaw_m));
            cam_right = XMVector3Normalize(XMVector3Cross(cam_up, cam_view));

            XMMATRIX pitch_m = XMMatrixRotationAxis(cam_right, pitch);

            offset = XMVectorSubtract(cam_pos, target);
            offset = XMVector3TransformCoord(offset, pitch_m);
            cam_pos = XMVectorAdd(target, offset);

            cam_view = XMVector3Normalize(XMVector3Transform(cam_view, pitch_m));
            cam_up = XMVector3Normalize(XMVector3Transform(cam_up, pitch_m));

            XMStoreFloat3(&out_cam_state.cam_pos, cam_pos);
            XMStoreFloat3(&out_cam_state.cam_view, cam_view);
            XMStoreFloat3(&out_cam_state.cam_up, cam_up);
#endif
        }

    private:
        static void SetControllerStateThunk(IPlugin* self, const ControllerState& in_controller_state)
        {
            return static_cast<CameraController*>(self)->SetControllerState(in_controller_state);
        }
        static void StartThunk(IPlugin* self, const XMFLOAT2& mouse_pos, const CameraState& in_cam_state)
        {
            return static_cast<CameraController*>(self)->Start(mouse_pos, in_cam_state);
        }
        static void PanMoveThunk(IPlugin* self, const XMFLOAT2& mouse_pos, CameraState& out_cam_state)
        {
            return static_cast<CameraController*>(self)->PanMove(mouse_pos, out_cam_state);
        }
        static void RotateThunk(IPlugin* self, const XMFLOAT2& mouse_pos, CameraState& out_cam_state)
        {
            return static_cast<CameraController*>(self)->Rotate(mouse_pos, out_cam_state);
        }

        inline static CameraControllerAPI s_api{
            &SetControllerStateThunk,
            &StartThunk,
            &PanMoveThunk,
            &RotateThunk
        };

        ControllerStateInternal internal_controller_state;
        CameraStateInternal internal_camera_state;
    };

    IMPLEMENT_PLUGIN(CameraController, PCameraController);
}