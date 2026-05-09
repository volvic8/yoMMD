#ifndef VIEWER_HPP_
#define VIEWER_HPP_

#include <filesystem>
#include <functional>
#include <map>
#include <random>
#include <unordered_set>
#include "Saba/Model/MMD/MMDMaterial.h"
#include "Saba/Model/MMD/MMDModel.h"
#include "Saba/Model/MMD/MMDNode.h"
#include "Saba/Model/MMD/VMDAnimation.h"
#include "Saba/Model/MMD/VMDCameraAnimation.h"
#include "config.hpp"
#include "image.hpp"
#include "sokol_gfx.h"
#include "util.hpp"

// Wrapper of sg_image and sg_view.
// Holds both sg_image and sg_view object.
class SgImageView {
public:
    SgImageView();
    SgImageView(const sg_image_desc& img_desc);
    SgImageView(const Image& image);

    const sg_view& getView() const;
    const sg_image& getImage() const;

    void destroy();

private:
    using Container = struct {
        sg_image image;
        sg_view view;
    };

private:
    std::optional<Container> container_;
};

class Material {
public:
    explicit Material(const saba::MMDMaterial& mat);
    const saba::MMDMaterial& material;
    std::optional<SgImageView> texture;
    std::optional<SgImageView> spTexture;
    std::optional<SgImageView> toonTexture;
    bool textureHasAlpha;
};

class MMD : private NonCopyable {
public:
    using Path = std::filesystem::path;
    using Animation = std::
        pair<std::unique_ptr<saba::VMDAnimation>, std::unique_ptr<saba::VMDCameraAnimation>>;
    void LoadModel(const Path& modelPath, const Path& resourcePath);
    void LoadMotion(const std::vector<Path>& paths);
    void Reset();
    bool IsModelLoaded() const;
    const std::shared_ptr<saba::MMDModel> GetModel() const;
    const std::vector<Animation>& GetAnimations() const;

private:
    std::shared_ptr<saba::MMDModel> model_;
    std::vector<Animation> animations_;
};

// A class to emphasize the shown MMD model to indicate the application
// instance currently featured.
class ModelEmphasizer : private NonCopyable {
public:
    void Init();
    void Draw();
    void Terminate();

private:
    sg_bindings binds_;
    sg_pipeline pipeline_;
    sg_shader shader_;
};

enum class GesturePhase {
    Unknown,
    Begin,
    Ongoing,
    End,
};

// Treats extra transformations given by user interactions.
class UserView {
public:
    struct Callback {
        std::function<void()> OnRotationChanged;
    };
    struct State {
        float rotation = 0.0f;
        float modelYaw = 0.0f;
        float modelPitch = 0.0f;
        float scale = 1.0f;
        glm::vec2 translation = glm::vec2(0.0f, 0.0f);
    };

public:
    void SetCallback(const Callback& callback);

    // Get a transformer matrix of user's viewport.
    glm::mat4 GetViewportMatrix() const;

    // Get a transformer matrix of model world.
    glm::mat4 GetWorldViewMatrix() const;
    glm::mat4 GetModelMatrix() const;

    void OnGestureBegin();
    void OnGestureEnd();
    void OnMouseDragged();
    void OnViewDragged(bool adjustYaw, bool adjustPitch);
    void OnWheelScrolled(float delta);
    void OnGestureZoom(GesturePhase phase, float delta);
    void SetDefaultTranslation(glm::vec2 pos);
    void SetDefaultScaling(float scale);
    void SetRotation(float rotation);
    void SetModelDirection(float yaw, float pitch);
    void SetModelPivot(glm::vec3 pivot);
    void ResetPosition();
    float GetScale() const;
    float GetRotation() const;
    glm::vec2 GetTranslation() const;
    State GetState() const;
    void RestoreState(const State& state);

private:
    static bool isDifferentPoint(const glm::vec2& p1, const glm::vec2& p2);

    // Translate a position in the main window into one in the model world.
    static glm::vec2 toWorldCoord(const glm::vec2& src, const glm::vec2& translation);
    static glm::vec2 toWindowCoord(const glm::vec2& src, const glm::vec2& translation);

    // changeScale changes the scale of MMD model to "newScale".  The base
    // point of scaling is the "refpoint", which specified in the coodinate on
    // screen.
    void changeScale(float newScale, glm::vec2 refpoint);

    // changeRotation rotates the world view by "delta". The base point of
    // scaling is the "refpoint", which specified in the coodinate on screen.
    // NOTE: "delta" should be in radian, not in digree.
    // NOTE: Different from changeScale function, the first argument should be
    // amount of change of rotation, not a new rotateion.
    void changeRotation(float delta, glm::vec2 refpoint);

private:
    enum class Action {
        None,
        Drag,
        Zoom,
        Rotate,
        ViewRotate,
    };
    struct Transform {
        float rotation = 0.0f;  // View rotation in radian
        float modelYaw = 0.0f;
        float modelPitch = 0.0f;
        float scale = 1.0f;
        glm::vec3 translation = glm::vec3(0.0f, 0.0f, 0.0f);
    };
    struct ActionHelper {
        Action action = Action::None;
        glm::vec2 refPoint;
        Transform firstTransform;
    };

    Transform transform_, defaultTransform_;
    ActionHelper actionHelper_;
    glm::vec3 modelPivot_ = glm::vec3(0.0f, 0.0f, 0.0f);

    Callback callback_;
};

class Routine : private NonCopyable {
public:
    Routine();
    ~Routine();
    void Init();
    void Update();
    void Draw();
    void Terminate();
    void OnGestureBegin();
    void OnGestureEnd();
    void OnMouseDragged();
    void OnViewDragged();
    void OnWheelScrolled(float delta);
    void OnGestureZoom(GesturePhase phase, float delta);
    float GetModelScale() const;
    void ResetModelPosition();
    void ChangeModel(const std::filesystem::path& modelPath);
    void ChangeMotion(const std::vector<std::filesystem::path>& motionPaths);
    void RestoreDefaultMotions();
    std::vector<std::string> GetAvailableExpressions() const;
    void SetExpression(const std::string& expressionName);
    void ClearExpression();
    const std::optional<std::string>& GetExpression() const;
    void SetModelRotation(float rotation);
    void SetModelViewDirection(float yaw, float pitch);
    void SetReactionModeEnabled(bool enabled);
    bool IsReactionModeEnabled() const;
    void SetViewDirectionModeXEnabled(bool enabled);
    void SetViewDirectionModeYEnabled(bool enabled);
    bool IsViewDirectionModeXEnabled() const;
    bool IsViewDirectionModeYEnabled() const;
    void SetModelScale(float scale);
    glm::vec2 GetModelMenuAnchorPosition(const glm::vec2& windowSize) const;
    glm::vec4 GetModelInteractionBounds(const glm::vec2& windowSize) const;
    void ParseConfig(const CmdArgs& args);
    const Config& GetConfig() const;

private:
    using ImageMap = std::map<std::string, Image>;
    void initScene();
    void destroyScene();
    void reloadScene(const Config& config);
    void initBuffers();
    void initTextures();
    void initPipeline();
    void selectNextMotion();
    std::optional<ImageMap::const_iterator> loadImage(const std::string& path);
    std::optional<SgImageView> getTexture(const std::string& path);
    void updateGravity();
    void resolveLookAtBoneTargets();
    void applyLookAtBoneRotations(bool useBaseAnimation);
    void cancelReaction(bool restoreBaseDirection);
    void triggerReaction(float yawOffset, float pitchOffset);
    void updateReaction();
    void applyExpressionMorphWeights();

private:
    struct Camera {
        glm::vec3 eye;
        glm::vec3 center;
    };

    Config baseConfig_;
    Config config_;

    ModelEmphasizer modelEmphasizer_;

    UserView userView_;

    bool shouldTerminate_;
    bool reactionModeEnabled_;
    bool reactionModeSuspended_;
    bool viewDirectionModeXEnabled_;
    bool viewDirectionModeYEnabled_;

    const sg_pass_action passAction_;
    sg_shader shaderMMD_;

    std::vector<uint32_t> induces_;
    sg_buffer posVB_;  // VB stands for "vertex buffer"
    sg_buffer normVB_;
    sg_buffer uvVB_;
    sg_buffer ibo_;
    sg_pipeline pipeline_frontface_;
    sg_pipeline pipeline_bothface_;
    sg_bindings binds_;

    glm::mat4 viewMatrix_;        // For model-view transformation
    glm::mat4 projectionMatrix_;  // For projection transformation
    MMD mmd_;

    SgImageView dummyTex_;
    ImageMap texImages_;
    std::map<std::string, SgImageView> textures_;
    std::vector<Material> materials_;
    sg_sampler sampler_texture_;
    sg_sampler sampler_sphere_texture_;
    sg_sampler sampler_toon_texture_;

    Camera defaultCamera_;

    // Timers for animation.
    uint64_t timeBeginAnimation_;
    uint64_t timeLastFrame_;

    size_t motionID_;
    std::optional<size_t> defaultMotionID_;
    std::vector<std::string> availableExpressionNames_;
    std::unordered_set<std::string> availableExpressionNameSet_;
    std::unordered_set<std::string> expressionOverrideMorphNameSet_;
    std::optional<std::string> selectedExpressionName_;
    bool needBridgeMotions_;
    std::vector<unsigned int> motionWeights_;

    std::mt19937 rand_;
    std::uniform_int_distribution<size_t> randDist_;

    struct LookAtBoneTargets {
        std::vector<saba::MMDNode*> upperBodyNodes;
        saba::MMDNode* neck = nullptr;
        saba::MMDNode* head = nullptr;
        saba::MMDNode* bothEyes = nullptr;
        saba::MMDNode* leftEye = nullptr;
        saba::MMDNode* rightEye = nullptr;

        bool HasAny() const {
            return !upperBodyNodes.empty() || neck || head || bothEyes || leftEye || rightEye;
        }
    } lookAtBoneTargets_;

    struct ReactionState {
        bool active = false;
        float baseYaw = 0.0f;
        float basePitch = 0.0f;
        float currentYaw = 0.0f;
        float currentPitch = 0.0f;
    } reactionState_;
};

#endif  // VIEWER_HPP_
