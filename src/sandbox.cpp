#include "sandbox.h"

#include <sys/stat.h>   // stat
#include <algorithm>    // std::find
#include <math.h>

#include "window.h"

#include "io/pixels.h"
#include "tools/text.h"
#include "tools/shapes.h"

#include "glm/gtx/matrix_transform_2d.hpp"
#include "glm/gtx/rotate_vector.hpp"

#include "shaders/default.h"
#include "shaders/dynamic_billboard.h"
#include "shaders/histogram.h"
#include "shaders/wireframe2D.h"
#include "shaders/fxaa.h"

std::string default_scene_frag = default_scene_frag0 + default_scene_frag1 + default_scene_frag2 + default_scene_frag3;

// ------------------------------------------------------------------------- CONTRUCTOR
Sandbox::Sandbox(): 
    frag_index(-1), vert_index(-1), geom_index(-1),
    verbose(false), cursor(true), fxaa(false),
    // Main Vert/Frag/Geom
    m_frag_source(""), m_vert_source(""),
    // Buffers
    m_buffers_total(0),
    // PostProcessing
    m_postprocessing(false),
    // Geometry helpers
    m_billboard_vbo(nullptr), m_cross_vbo(nullptr),
    // Record
    m_record_fdelta(0.04166666667), m_record_start(0.0f), m_record_head(0.0f), m_record_end(0.0f), m_record_counter(0), m_record(false),
    // Histogram
    m_histogram_texture(nullptr), m_histogram(false),
    // Scene
    m_view2d(1.0), m_lat(180.0), m_lon(0.0), m_frame(0), m_change(true), m_initialized(false),
    // Debug
    m_showTextures(false), m_showPasses(false)
{

    // TIME UNIFORMS
    //
    uniforms.functions["u_time"] = UniformFunction( "float", 
    [this](Shader& _shader) {
        if (m_record) _shader.setUniform("u_time", m_record_head);
        else _shader.setUniform("u_time", float(getTime()));
    }, []() { return toString(getTime()); } );

    uniforms.functions["u_delta"] = UniformFunction("float", 
    [this](Shader& _shader) {
        if (m_record) _shader.setUniform("u_delta", float(m_record_fdelta));
        else _shader.setUniform("u_delta", float(getDelta()));
    },
    []() { return toString(getDelta()); });

    uniforms.functions["u_date"] = UniformFunction("vec4", [](Shader& _shader) {
        _shader.setUniform("u_date", getDate());
    },
    []() { return toString(getDate(), ','); });

    // MOUSE
    uniforms.functions["u_mouse"] = UniformFunction("vec2", [](Shader& _shader) {
        _shader.setUniform("u_mouse", getMouseX(), getMouseY());
    },
    []() { return toString(getMouseX()) + "," + toString(getMouseY()); } );

    // VIEWPORT
    uniforms.functions["u_resolution"]= UniformFunction("vec2", [](Shader& _shader) {
        _shader.setUniform("u_resolution", getWindowWidth(), getWindowHeight());
    },
    []() { return toString(getWindowWidth()) + "," + toString(getWindowHeight()); });

    // SCENE
    uniforms.functions["u_scene"] = UniformFunction("sampler2D", [this](Shader& _shader) {
        if (m_postprocessing && m_scene_fbo.getTextureId()) {
            _shader.setUniformTexture("u_scene", &m_scene_fbo, _shader.textureIndex++ );
        }
    });

    #if !defined(PLATFORM_RPI) && !defined(PLATFORM_RPI4)
    uniforms.functions["u_sceneDepth"] = UniformFunction("sampler2D", [this](Shader& _shader) {
        if (m_postprocessing && m_scene_fbo.getTextureId()) {
            _shader.setUniformDepthTexture("u_sceneDepth", &m_scene_fbo, _shader.textureIndex++ );
        }
    });

    uniforms.functions["u_lightShadowMap"] = UniformFunction("sampler2D", [this](Shader& _shader) {
        if (uniforms.lights.size() > 0) {
            _shader.setUniformDepthTexture("u_lightShadowMap", uniforms.lights[0].getShadowMap(), _shader.textureIndex++ );
        }
    });
    #endif

    uniforms.functions["u_view2d"] = UniformFunction("mat3", [this](Shader& _shader) {
        _shader.setUniform("u_view2d", m_view2d);
    });

    uniforms.functions["u_modelViewProjectionMatrix"] = UniformFunction("mat4");
}

Sandbox::~Sandbox() {
}

// ------------------------------------------------------------------------- SET

void Sandbox::setup( WatchFileList &_files, CommandList &_commands ) {

    // Add Sandbox Commands
    // ----------------------------------------
    _commands.push_back(Command("debug", [&](const std::string& _line){
        if (_line == "debug") {
            std::string rta = m_showPasses ? "on" : "off";
            std::cout << "buffers," << rta << std::endl; 
            rta = m_showTextures ? "on" : "off";
            std::cout << "textures," << rta << std::endl; 
            if (geom_index != -1) {
                rta = m_scene.showGrid ? "on" : "off";
                std::cout << "grid," << rta << std::endl; 
                rta = m_scene.showAxis ? "on" : "off";
                std::cout << "axis," << rta << std::endl; 
                rta = m_scene.showBBoxes ? "on" : "off";
                std::cout << "bboxes," << rta << std::endl;
            }
            return true;
        }
        else {
            std::vector<std::string> values = split(_line,',');
            if (values.size() == 2) {
                m_showPasses = (values[1] == "on");
                m_showTextures = (values[1] == "on");
                m_histogram = (values[1] == "on");
                if (geom_index != -1) {
                    m_scene.showGrid = (values[1] == "on");
                    m_scene.showAxis = (values[1] == "on");
                    m_scene.showBBoxes = (values[1] == "on");
                    if (values[1] == "on") {
                        m_scene.addDefine("DEBUG", values[1]);
                    }
                    else {
                        m_scene.delDefine("DEBUG");
                    }
                }
            }
        }
        return false;
    },
    "debug[,on|off]                 show/hide passes and textures elements", false));

    _commands.push_back(Command("histogram", [&](const std::string& _line){
        if (_line == "histogram") {
            std::string rta = m_histogram ? "on" : "off";
            std::cout << "histogram," << rta << std::endl; 
            return true;
        }
        else {
            std::vector<std::string> values = split(_line,',');
            if (values.size() == 2) {
                m_histogram = (values[1] == "on");
            }
        }
        return false;
    },
    "histogram[,on|off]             show/hide histogram", false));

    _commands.push_back(Command("defines", [&](const std::string& _line){ 
        if (_line == "defines") {
            if (geom_index == -1)
                m_canvas_shader.printDefines();
            else
                m_scene.printDefines();
            return true;
        }
        return false;
    },
    "defines                        return a list of active defines", false));
    
    _commands.push_back( Command("define,", [&](const std::string& _line){ 
        std::vector<std::string> values = split(_line,',');
        if (values.size() == 2) {
            std::vector<std::string> v = split(values[1],' ');
            addDefine( v[0], v[1] );
            return true;
        }
        else if (values.size() == 3) {
            addDefine( values[1], values[2] );
            return true;
        }
        return false;
    },
    "define,<KEYWORD>               add a define to the shader", false));

    _commands.push_back( Command("undefine,", [&](const std::string& _line){ 
        std::vector<std::string> values = split(_line,',');
        if (values.size() == 2) {
            delDefine( values[1] );
            return true;
        }
        return false;
    },
    "undefine,<KEYWORD>             remove a define on the shader", false));

    _commands.push_back(Command("uniforms", [&](const std::string& _line){ 
        uniforms.print(_line == "uniforms,all");
        return true;
    },
    "uniforms[,all|active]          return a list of all or active uniforms and their values.", false));

    _commands.push_back(Command("textures", [&](const std::string& _line){ 
        if (_line == "textures") {
            uniforms.printTextures();
            return true;
        }
        else {
            std::vector<std::string> values = split(_line,',');
            if (values.size() == 2) {
                m_showTextures = (values[1] == "on");
            }
        }
        return false;
    },
    "textures                       return a list of textures as their uniform name and path.", false));

    _commands.push_back(Command("buffers", [&](const std::string& _line){ 
        if (_line == "buffers") {
            uniforms.printBuffers();
            if (m_postprocessing) {
                if (fxaa)
                    std::cout << "FXAA";
                else
                    std::cout << "Custom";
                std::cout << " postProcessing pass" << std::endl;
            }
            
            return true;
        }
        else {
            std::vector<std::string> values = split(_line,',');
            if (values.size() == 2) {
                m_showPasses = (values[1] == "on");
            }
        }
        return false;
    },
    "buffers                        return a list of buffers as their uniform name.", false));

    // LIGTH
    _commands.push_back(Command("lights", [&](const std::string& _line){ 
        if (_line == "lights") {
            uniforms.printLights();
            return true;
        }
        return false;
    },
    "lights                         get all light data."));

    _commands.push_back(Command("light_position", [&](const std::string& _line){ 
        std::vector<std::string> values = split(_line,',');
        if (values.size() == 4) {
            if (uniforms.lights.size() > 0) 
                uniforms.lights[0].setPosition(glm::vec3(toFloat(values[1]),toFloat(values[2]),toFloat(values[3])));
            return true;
        }
        else if (values.size() == 5) {
            unsigned int i = toInt(values[1]);
            if (uniforms.lights.size() > i) 
                uniforms.lights[i].setPosition(glm::vec3(toFloat(values[2]),toFloat(values[3]),toFloat(values[4])));
            return true;
        }
        else {
            if (uniforms.lights.size() > 0) {
                glm::vec3 pos = uniforms.lights[0].getPosition();
                std::cout << ',' << pos.x << ',' << pos.y << ',' << pos.z << std::endl;
            }
            return true;
        }
        return false;
    },
    "light_position[,<x>,<y>,<z>]   get or set the light position."));

    _commands.push_back(Command("light_color", [&](const std::string& _line){ 
         std::vector<std::string> values = split(_line,',');
        if (values.size() == 4) {
            if (uniforms.lights.size() > 0) {
                uniforms.lights[0].color = glm::vec3(toFloat(values[1]),toFloat(values[2]),toFloat(values[3]));
                uniforms.lights[0].bChange = true;
            }
            return true;
        }
        else if (values.size() == 5) {
            unsigned int i = toInt(values[1]);
            if (uniforms.lights.size() > i) {
                uniforms.lights[i].color = glm::vec3(toFloat(values[2]),toFloat(values[3]),toFloat(values[4]));
                uniforms.lights[i].bChange = true;
            }
            return true;
        }
        else {
            if (uniforms.lights.size() > 0) {
                glm::vec3 color = uniforms.lights[0].color;
                std::cout << color.x << ',' << color.y << ',' << color.z << std::endl;
            }
            
            return true;
        }
        return false;
    },
    "light_color[,<r>,<g>,<b>]      get or set the light color."));

    _commands.push_back(Command("light_falloff", [&](const std::string& _line){ 
         std::vector<std::string> values = split(_line,',');
        if (values.size() == 2) {
            if (uniforms.lights.size() > 0) {
                uniforms.lights[0].falloff = toFloat(values[1]);
                uniforms.lights[0].bChange = true;
            }
            return true;
        }
        else if (values.size() == 5) {
            unsigned int i = toInt(values[1]);
            if (uniforms.lights.size() > i) {
                uniforms.lights[i].falloff = toFloat(values[2]);
                uniforms.lights[i].bChange = true;
            }
            return true;
        }
        else {
            if (uniforms.lights.size() > 0) {
                std::cout <<  uniforms.lights[0].falloff << std::endl;
            }
            return true;
        }
        return false;
    },
    "light_falloff[,<value>]        get or set the light falloff distance."));

    _commands.push_back(Command("light_intensity", [&](const std::string& _line){ 
         std::vector<std::string> values = split(_line,',');
        if (values.size() == 2) {
            if (uniforms.lights.size() > 0) {
                uniforms.lights[0].intensity = toFloat(values[1]);
                uniforms.lights[0].bChange = true;
            }
            return true;
        }
        else if (values.size() == 5) {
            unsigned int i = toInt(values[1]);
            if (uniforms.lights.size() > i) {
                uniforms.lights[i].intensity = toFloat(values[2]);
                uniforms.lights[i].bChange = true;
            }
            return true;
        }
        else {
            if (uniforms.lights.size() > 0) {
                std::cout <<  uniforms.lights[0].intensity << std::endl;
            }
            
            return true;
        }
        return false;
    },
    "light_intensity[,<value>]      get or set the light intensity."));

    // CAMERA
    _commands.push_back(Command("camera_distance", [&](const std::string& _line){ 
        std::vector<std::string> values = split(_line,',');
        if (values.size() == 2) {
            uniforms.getCamera().setDistance(toFloat(values[1]));
            return true;
        }
        else {
            std::cout << uniforms.getCamera().getDistance() << std::endl;
            return true;
        }
        return false;
    },
    "camera_distance[,<dist>]       get or set the camera distance to the target."));

    _commands.push_back(Command("camera_fov", [&](const std::string& _line){ 
        std::vector<std::string> values = split(_line,',');
        if (values.size() == 2) {
            uniforms.getCamera().setFOV(toFloat(values[1]));
            return true;
        }
        else {
            std::cout << uniforms.getCamera().getFOV() << std::endl;
            return true;
        }
        return false;
    },
    "camera_fov[,<field_of_view>]   get or set the camera field of view."));

    _commands.push_back(Command("camera_position", [&](const std::string& _line){ 
        std::vector<std::string> values = split(_line,',');
        if (values.size() == 4) {
            uniforms.getCamera().setPosition(glm::vec3(toFloat(values[1]),toFloat(values[2]),toFloat(values[3])));
            uniforms.getCamera().lookAt(uniforms.getCamera().getTarget());
            return true;
        }
        else {
            glm::vec3 pos = uniforms.getCamera().getPosition();
            std::cout << pos.x << ',' << pos.y << ',' << pos.z << std::endl;
            return true;
        }
        return false;
    },
    "camera_position[,<x>,<y>,<z>]  get or set the camera position."));

    _commands.push_back(Command("camera_exposure", [&](const std::string& _line){ 
        std::vector<std::string> values = split(_line,',');
        if (values.size() == 4) {
            uniforms.getCamera().setExposure(toFloat(values[1]),toFloat(values[2]),toFloat(values[3]));
            return true;
        }
        else {
            std::cout << uniforms.getCamera().getExposure() << std::endl;
            return true;
        }
        return false;
    },
    "camera_exposure[,<aper.>,<shutter>,<sensit.>]  get or set the camera exposure values."));

    // LOAD SHACER 
    // -----------------------------------------------

    if (vert_index != -1) {
        // If there is a Vertex shader load it
        m_vert_source = "";
        m_vert_dependencies.clear();

        loadFromPath(_files[vert_index].path, &m_vert_source, include_folders, &m_vert_dependencies);
    }
    else {
        // If there is no use the default one
        if (geom_index == -1)
            m_vert_source = default_vert;
        else
            m_vert_source = default_scene_vert;
    }

    if (frag_index != -1) {
        // If there is a Fragment shader load it
        m_frag_source = "";
        m_frag_dependencies.clear();

        if ( !loadFromPath(_files[frag_index].path, &m_frag_source, include_folders, &m_frag_dependencies) ) {
            return;
        }
    }
    else {
        // If there is no use the default one
        if (geom_index == -1)
            m_frag_source = default_frag;
        else
            m_frag_source = default_scene_frag;
    }

    // Init Scene elements
    m_billboard_vbo = rect(0.0,0.0,1.0,1.0).getVbo();

    // LOAD GEOMETRY
    // -----------------------------------------------

    if (geom_index == -1) {
        // m_canvas_shader.addDefine("MODEL_VERTEX_EX_COLORS");
        // m_canvas_shader.addDefine("MODEL_VERTEX_EX_NORMALS");
        m_canvas_shader.addDefine("MODEL_VERTEX_TEXCOORD");
        // m_canvas_shader.addDefine("MODEL_VERTEX_TANGENT");
    }
    else {
        m_scene.setup( _commands, uniforms);
        m_scene.loadGeometry( uniforms, _files, geom_index, verbose );
    }

    // FINISH SCENE SETUP
    // -------------------------------------------------
    uniforms.getCamera().setViewport(getWindowWidth(), getWindowHeight());

    // Prepare viewport
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    glFrontFace(GL_CCW);

    // Turn on Alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Clear the background
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // LOAD SHADERS
    reloadShaders( _files );

    // TODO:
    //      - this seams to solve the problem of buffers not properly initialize
    //      - digg deeper
    //
    uniforms.buffers.clear();
    _updateBuffers();

    flagChange();
}

void Sandbox::addDefine(const std::string &_define, const std::string &_value) {
    for (int i = 0; i < m_buffers_total; i++)
        m_buffers_shaders[i].addDefine(_define, _value);

    if (geom_index == -1)
        m_canvas_shader.addDefine(_define, _value);
    else
        m_scene.addDefine(_define, _value);

    m_postprocessing_shader.addDefine(_define, _value);
}

void Sandbox::delDefine(const std::string &_define) {
    for (int i = 0; i < m_buffers_total; i++)
        m_buffers_shaders[i].delDefine(_define);

    if (geom_index == -1)
        m_canvas_shader.delDefine(_define);
    else
        m_scene.delDefine(_define);

    m_postprocessing_shader.delDefine(_define);
}

// ------------------------------------------------------------------------- GET

bool Sandbox::isReady() {
    return m_initialized;
}

void Sandbox::flagChange() { 
    m_change = true;
}

void Sandbox::unflagChange() {
    m_change = false;
    m_scene.unflagChange();
    uniforms.unflagChange();
}

bool Sandbox::haveChange() { 

    // std::cout << "CHANGE " << m_change << std::endl;
    // std::cout << "RECORD " << m_record << std::endl;
    // std::cout << "SCENE " << m_scene.haveChange() << std::endl;
    // std::cout << "UNIFORMS " << uniforms.haveChange() << std::endl;
    // std::cout << std::endl;

    return  m_change ||
            m_record ||
            screenshotFile != "" ||
            m_scene.haveChange() ||
            uniforms.haveChange();
}

std::string Sandbox::getSource(ShaderType _type) const {
    if (_type == FRAGMENT) return m_frag_source;
    else return m_vert_source;
}

int Sandbox::getRecordedPorcentage() {
    return ((m_record_head - m_record_start) / (m_record_end - m_record_start)) * 100;
}

// ------------------------------------------------------------------------- RELOAD SHADER

bool Sandbox::reloadShaders( WatchFileList &_files ) {
    flagChange();

    // UPDATE scene shaders of models (materials)
    if (geom_index == -1) {

        if (verbose)
            std::cout << "// Reload 2D shaders" << std::endl;

        // Reload the shader
        m_canvas_shader.detach(GL_FRAGMENT_SHADER | GL_VERTEX_SHADER);
        m_canvas_shader.load(m_frag_source, m_vert_source, verbose);
    }
    else {
        if (verbose)
            std::cout << "// Reload 3D scene shaders" << std::endl;

        m_scene.loadShaders(m_frag_source, m_vert_source, verbose);
    }

    // UPDATE shaders dependencies
    {
        List new_dependencies = merge(m_frag_dependencies, m_vert_dependencies);

        // remove old dependencies
        for (int i = _files.size() - 1; i >= 0; i--)
            if (_files[i].type == GLSL_DEPENDENCY)
                _files.erase( _files.begin() + i);

        // Add new dependencies
        struct stat st;
        for (unsigned int i = 0; i < new_dependencies.size(); i++) {
            WatchFile file;
            file.type = GLSL_DEPENDENCY;
            file.path = new_dependencies[i];
            stat( file.path.c_str(), &st );
            file.lastChange = st.st_mtime;
            _files.push_back(file);

            if (verbose)
                std::cout << " Watching file " << new_dependencies[i] << " as a dependency " << std::endl;
        }
    }

    // UPDATE uniforms
    uniforms.checkPresenceIn(m_vert_source, m_frag_source); // Check active native uniforms
    uniforms.flagChange();                                  // Flag all user defined uniforms as changed

    if (uniforms.cubemap) {
        addDefine("SCENE_SH_ARRAY", "u_SH");
        addDefine("SCENE_CUBEMAP", "u_cubeMap");
    }

    // UPDATE Buffers
    m_buffers_total = count_buffers(m_frag_source);
    _updateBuffers();
    
    // UPDATE Postprocessing
    bool havePostprocessing = check_for_postprocessing(getSource(FRAGMENT));
    if (havePostprocessing) {
        // Specific defines for this buffer
        m_postprocessing_shader.addDefine("POSTPROCESSING");
        m_postprocessing_shader.load(m_frag_source, billboard_vert, false);
        m_postprocessing = havePostprocessing;
    }
    else if (fxaa) {
        m_postprocessing_shader.load(fxaa_frag, billboard_vert, false);
        uniforms.functions["u_scene"].present = true;
        m_postprocessing = true;
    }
    else 
        m_postprocessing = false;

    if (m_postprocessing || m_histogram) { //|| uniforms.functions["u_scene"].present) {
        FboType type = uniforms.functions["u_sceneDepth"].present ? COLOR_DEPTH_TEXTURES : COLOR_TEXTURE_DEPTH_BUFFER;
        if (!m_scene_fbo.isAllocated() || m_scene_fbo.getType() != type)
            m_scene_fbo.allocate(getWindowWidth(), getWindowHeight(), type);
    }

    return true;
}

// ------------------------------------------------------------------------- UPDATE
void Sandbox::_updateBuffers() {
    if ( m_buffers_total != int(uniforms.buffers.size()) ) {

        if (verbose)
            std::cout << " Creating/Removing " << uniforms.buffers.size() << " buffers to " << m_buffers_total << std::endl;

        uniforms.buffers.clear();
        m_buffers_shaders.clear();

        for (int i = 0; i < m_buffers_total; i++) {
            // New FBO
            uniforms.buffers.push_back( Fbo() );
            uniforms.buffers[i].allocate(getWindowWidth(), getWindowHeight(), COLOR_TEXTURE);
            
            // New Shader
            m_buffers_shaders.push_back( Shader() );
            m_buffers_shaders[i].addDefine("BUFFER_" + toString(i));
            m_buffers_shaders[i].load(m_frag_source, billboard_vert, false);
        }
    }
    else {
        for (unsigned int i = 0; i < m_buffers_shaders.size(); i++) {

            // Reload shader code
            m_buffers_shaders[i].addDefine("BUFFER_" + toString(i));
            m_buffers_shaders[i].load(m_frag_source, billboard_vert, false);
        }
    }
}

// ------------------------------------------------------------------------- DRAW
void Sandbox::_renderBuffers() {
    glDisable(GL_BLEND);

    for (unsigned int i = 0; i < uniforms.buffers.size(); i++) {
        uniforms.buffers[i].bind();
        m_buffers_shaders[i].use();

        // Update uniforms and textures
        uniforms.feedTo( m_buffers_shaders[i] );

        // Pass textures for the other buffers
        for (unsigned int j = 0; j < uniforms.buffers.size(); j++) {
            if (i != j) {
                m_buffers_shaders[i].setUniformTexture("u_buffer" + toString(j), &uniforms.buffers[j] );
            }
        }

        m_billboard_vbo->render( &m_buffers_shaders[i] );

        uniforms.buffers[i].unbind();
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Sandbox::render() {
    // RENDER SHADOW MAP
    // -----------------------------------------------
    if (geom_index != -1)
        if (uniforms.functions["u_lightShadowMap"].present)
            m_scene.renderShadowMap(uniforms);
    
    // BUFFERS
    // -----------------------------------------------
    if (uniforms.buffers.size() > 0)
        _renderBuffers();
    
    // MAIN SCENE
    // ----------------------------------------------- < main scene start
    if (screenshotFile != "" || m_record)
        if (!m_record_fbo.isAllocated())
            m_record_fbo.allocate(getWindowWidth(), getWindowHeight(), COLOR_TEXTURE_DEPTH_BUFFER);

    if (m_postprocessing || m_histogram ) {
        if (!m_scene_fbo.isAllocated()) {
            FboType type = uniforms.functions["u_sceneDepth"].present ? COLOR_DEPTH_TEXTURES : COLOR_TEXTURE_DEPTH_BUFFER;
            m_scene_fbo.allocate(getWindowWidth(), getWindowHeight(), type);
        }

        m_scene_fbo.bind();
    }
    else if (screenshotFile != "" || m_record )
        m_record_fbo.bind();

    // Clear the background
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // RENDER CONTENT
    if (geom_index == -1) {
        // Load main shader
        m_canvas_shader.use();

        // Update Uniforms and textures variables
        uniforms.feedTo( m_canvas_shader );

        // Pass special uniforms
        m_canvas_shader.setUniform("u_modelViewProjectionMatrix", glm::mat4(1.));
        m_billboard_vbo->render( &m_canvas_shader );
    }
    else {
        m_scene.render(uniforms);
        if (m_scene.showGrid || m_scene.showAxis || m_scene.showBBoxes)
            m_scene.renderDebug(uniforms);
    }
    
    // ----------------------------------------------- < main scene end

    // POST PROCESSING
    if (m_postprocessing) {
        m_scene_fbo.unbind();

        if (screenshotFile != "" || m_record)
            m_record_fbo.bind();
    
        m_postprocessing_shader.use();

        // Update uniforms and textures
        uniforms.feedTo( m_postprocessing_shader );

        // Pass textures of buffers
        for (unsigned int i = 0; i < uniforms.buffers.size(); i++)
            m_postprocessing_shader.setUniformTexture("u_buffer" + toString(i), &uniforms.buffers[i]);

        m_billboard_vbo->render( &m_postprocessing_shader );
    }
    else if (m_histogram) {
        m_scene_fbo.unbind();

        if (screenshotFile != "" || m_record)
            m_record_fbo.bind();

        if (!m_billboard_shader.isLoaded())
            m_billboard_shader.load(dynamic_billboard_frag, dynamic_billboard_vert, false);

        m_billboard_shader.use();
        m_billboard_shader.setUniform("u_depth", float(0.0));
        m_billboard_shader.setUniform("u_scale", 1.0, 1.0);
        m_billboard_shader.setUniform("u_translate", 0.0, 0.0);
        m_billboard_shader.setUniform("u_modelViewProjectionMatrix", glm::mat4(1.0) );
        m_billboard_shader.setUniformTexture("u_tex0", &m_scene_fbo, 0);
        m_billboard_vbo->render( &m_billboard_shader );
    }
    
    if (screenshotFile != "" || m_record) {
        m_record_fbo.unbind();

        if (!m_billboard_shader.isLoaded())
            m_billboard_shader.load(dynamic_billboard_frag, dynamic_billboard_vert, false);

        m_billboard_shader.use();
        m_billboard_shader.setUniform("u_depth", float(0.0));
        m_billboard_shader.setUniform("u_scale", 1.0, 1.0);
        m_billboard_shader.setUniform("u_translate", 0.0, 0.0);
        m_billboard_shader.setUniform("u_modelViewProjectionMatrix", glm::mat4(1.0) );
        m_billboard_shader.setUniformTexture("u_tex0", &m_record_fbo, 0);
        m_billboard_vbo->render( &m_billboard_shader );
    }
}


void Sandbox::renderUI() {
    if (m_showPasses) {        
        glDisable(GL_DEPTH_TEST);

        // DEBUG BUFFERS
        int nTotal = uniforms.buffers.size();
        if (m_postprocessing) {
            nTotal += uniforms.functions["u_scene"].present;
            nTotal += uniforms.functions["u_sceneDepth"].present;
        }
        nTotal += uniforms.functions["u_lightShadowMap"].present;
        if (nTotal > 0) {
            float w = (float)(getWindowWidth());
            float h = (float)(getWindowHeight());
            float scale = fmin(1.0f / (float)(nTotal), 0.25) * 0.5;
            float xStep = w * scale;
            float yStep = h * scale;
            float xOffset = xStep;
            float yOffset = h - yStep;

            if (!m_billboard_shader.isLoaded())
                m_billboard_shader.load(dynamic_billboard_frag, dynamic_billboard_vert, false);

            m_billboard_shader.use();

            for (unsigned int i = 0; i < uniforms.buffers.size(); i++) {
                m_billboard_shader.setUniform("u_depth", float(0.0));
                m_billboard_shader.setUniform("u_scale", xStep, yStep);
                m_billboard_shader.setUniform("u_translate", xOffset, yOffset);
                m_billboard_shader.setUniform("u_modelViewProjectionMatrix", getOrthoMatrix());
                m_billboard_shader.setUniformTexture("u_tex0", &uniforms.buffers[i]);
                m_billboard_vbo->render(&m_billboard_shader);
                yOffset -= yStep * 2.0;
            }

            if (m_postprocessing) {
                if (uniforms.functions["u_scene"].present) {
                    m_billboard_shader.setUniform("u_depth", float(0.0));
                    m_billboard_shader.setUniform("u_scale", xStep, yStep);
                    m_billboard_shader.setUniform("u_translate", xOffset, yOffset);
                    m_billboard_shader.setUniform("u_modelViewProjectionMatrix", getOrthoMatrix());
                    m_billboard_shader.setUniformTexture("u_tex0", &m_scene_fbo, 0);
                    m_billboard_vbo->render(&m_billboard_shader);
                    yOffset -= yStep * 2.0;
                }

                #if !defined(PLATFORM_RPI) && !defined(PLATFORM_RPI4)
                if (uniforms.functions["u_sceneDepth"].present) {
                    m_billboard_shader.setUniform("u_scale", xStep, yStep);
                    m_billboard_shader.setUniform("u_translate", xOffset, yOffset);
                    m_billboard_shader.setUniform("u_depth", float(1.0));
                    uniforms.functions["u_cameraNearClip"].assign(m_billboard_shader);
                    uniforms.functions["u_cameraFarClip"].assign(m_billboard_shader);
                    uniforms.functions["u_cameraDistance"].assign(m_billboard_shader);
                    m_billboard_shader.setUniform("u_modelViewProjectionMatrix", getOrthoMatrix());
                    m_billboard_shader.setUniformDepthTexture("u_tex0", &m_scene_fbo);
                    m_billboard_vbo->render(&m_billboard_shader);
                    yOffset -= yStep * 2.0;
                }
                #endif
            }

        #if !defined(PLATFORM_RPI) && !defined(PLATFORM_RPI4) 
            if (uniforms.functions["u_lightShadowMap"].present) {
                float x = xOffset;
                float y = (float)(getWindowHeight()) - xOffset;
                float w = xOffset;
                float h = xOffset;

                for (unsigned int i = 0; i < uniforms.lights.size(); i++) {
                    if ( uniforms.lights[i].getShadowMap()->getDepthTextureId() ) {
                        m_billboard_shader.setUniform("u_scale", w, h);
                        m_billboard_shader.setUniform("u_translate", x, y);
                        m_billboard_shader.setUniform("u_depth", float(0.0));
                        m_billboard_shader.setUniform("u_modelViewProjectionMatrix", getOrthoMatrix());
                        m_billboard_shader.setUniformDepthTexture("u_tex0", uniforms.lights[i].getShadowMap());
                        m_billboard_vbo->render(&m_billboard_shader);
                        x += w;
                    }
                }
            }
        #endif
        }
    }

    if (m_histogram && m_histogram_texture) {
        glDisable(GL_DEPTH_TEST);

        float w = 100;
        float h = 50;
        float x = (float)(getWindowWidth()) * 0.5;
        float y = h;

        if (!m_histogram_shader.isLoaded())
            m_histogram_shader.load(histogram_frag, dynamic_billboard_vert, false);

        m_histogram_shader.use();
        for (std::map<std::string, Texture*>::iterator it = uniforms.textures.begin(); it != uniforms.textures.end(); it++) {
            m_histogram_shader.setUniform("u_scale", w, h);
            m_histogram_shader.setUniform("u_translate", x, y);
            m_histogram_shader.setUniform("u_resolution", (float)getWindowWidth(), (float)getWindowHeight());
            m_histogram_shader.setUniform("u_modelViewProjectionMatrix", getOrthoMatrix());
            m_histogram_shader.setUniformTexture("u_sceneHistogram", m_histogram_texture, 0);
            m_billboard_vbo->render(&m_histogram_shader);
        }
    }

    if (m_showTextures) {        
        glDisable(GL_DEPTH_TEST);

        int nTotal = uniforms.textures.size();
        if (nTotal > 0) {
            float w = (float)(getWindowWidth());
            float h = (float)(getWindowHeight());
            float scale = fmin(1.0f / (float)(nTotal), 0.25) * 0.5;
            float yStep = h * scale;
            float xStep = h * scale;
            float xOffset = w - xStep;
            float yOffset = h - yStep;

            if (!m_billboard_shader.isLoaded())
                m_billboard_shader.load(dynamic_billboard_frag, dynamic_billboard_vert, false);

            m_billboard_shader.use();

            for (std::map<std::string, Texture*>::iterator it = uniforms.textures.begin(); it != uniforms.textures.end(); it++) {
                m_billboard_shader.setUniform("u_depth", float(0.0));
                m_billboard_shader.setUniform("u_scale", xStep, yStep);
                m_billboard_shader.setUniform("u_translate", xOffset, yOffset);
                m_billboard_shader.setUniform("u_modelViewProjectionMatrix", getOrthoMatrix());
                m_billboard_shader.setUniformTexture("u_tex0", it->second, 0);
                m_billboard_vbo->render(&m_billboard_shader);
                yOffset -= yStep * 2.0;
            }
        }
    }

    if (cursor) {
        if (m_cross_vbo == nullptr) 
            m_cross_vbo = cross(glm::vec3(0.0, 0.0, 0.0), 10.).getVbo();

        if (!m_wireframe2D_shader.isLoaded())
            m_wireframe2D_shader.load(wireframe2D_frag, wireframe2D_vert, false);

        glLineWidth(2.0f);
        m_wireframe2D_shader.use();
        m_wireframe2D_shader.setUniform("u_color", glm::vec4(1.0));
        m_wireframe2D_shader.setUniform("u_scale", 1.0f, 1.0f);
        m_wireframe2D_shader.setUniform("u_translate", getMouseX(), getMouseY());
        m_wireframe2D_shader.setUniform("u_modelViewProjectionMatrix", getOrthoMatrix());
        m_cross_vbo->render(&m_wireframe2D_shader);
        glLineWidth(1.0f);
    }
}

void Sandbox::renderDone() {
    // RECORD
    if (m_record) {
        onScreenshot(toString(m_record_counter, 0, 5, '0') + ".png");

        m_record_head += m_record_fdelta;
        m_record_counter++;

        if (m_record_head >= m_record_end) {
            m_record = false;
        }
    }
    // SCREENSHOT 
    else if (screenshotFile != "") {
        onScreenshot(screenshotFile);
        screenshotFile = "";
    }

    if (m_histogram)
        onHistogram();

    m_frame++;

    unflagChange();

    if (!m_initialized) {
        m_initialized = true;
        updateViewport();
        flagChange();
    }
}

// ------------------------------------------------------------------------- ACTIONS

void Sandbox::clear() {
    uniforms.clear();

    if (geom_index != -1)
        m_scene.clear();

    if (m_billboard_vbo)
        delete m_billboard_vbo;

    if (m_cross_vbo)
        delete m_cross_vbo;
}

void Sandbox::record(float _start, float _end, float fps) {
    m_record_fdelta = 1.0/fps;
    m_record_start = _start;
    m_record_head = _start;
    m_record_end = _end;
    m_record_counter = 0;
    m_record = true;
}

void Sandbox::printDependencies(ShaderType _type) const {
    if (_type == FRAGMENT) {
        for (unsigned int i = 0; i < m_frag_dependencies.size(); i++) {
            std::cout << m_frag_dependencies[i] << std::endl;
        }
    }
    else {
        for (unsigned int i = 0; i < m_vert_dependencies.size(); i++) {
            std::cout << m_vert_dependencies[i] << std::endl;
        }
    }
}

// ------------------------------------------------------------------------- EVENTS

void Sandbox::onFileChange(WatchFileList &_files, int index) {
    FileType type = _files[index].type;
    std::string filename = _files[index].path;

    // IF the change is on a dependency file, re route to the correct shader that need to be reload
    if (type == GLSL_DEPENDENCY) {
        if (std::find(m_frag_dependencies.begin(), m_frag_dependencies.end(), filename) != m_frag_dependencies.end()) {
            type = FRAG_SHADER;
            filename = _files[frag_index].path;
        }
        else if(std::find(m_vert_dependencies.begin(), m_vert_dependencies.end(), filename) != m_vert_dependencies.end()) {
            type = VERT_SHADER;
            filename = _files[vert_index].path;
        }
    }
    
    if (type == FRAG_SHADER) {
        m_frag_source = "";
        m_frag_dependencies.clear();
        if ( loadFromPath(filename, &m_frag_source, include_folders, &m_frag_dependencies) )
            reloadShaders(_files);
    }
    else if (type == VERT_SHADER) {
        m_vert_source = "";
        m_vert_dependencies.clear();
        if ( loadFromPath(filename, &m_vert_source, include_folders, &m_vert_dependencies) )
            reloadShaders(_files);
    }
    else if (type == GEOMETRY) {
        // TODO
    }
    else if (type == IMAGE) {
        for (TextureList::iterator it = uniforms.textures.begin(); it!=uniforms.textures.end(); it++) {
            if (filename == it->second->getFilePath()) {
                std::cout << filename << std::endl;
                it->second->load(filename, _files[index].vFlip);
                break;
            }
        }
    }
    else if (type == CUBEMAP) {
        if (uniforms.cubemap)
            uniforms.cubemap->load(filename, _files[index].vFlip);
    }

    flagChange();
}

void Sandbox::onScroll(float _yoffset) {
    // Vertical scroll button zooms u_view2d and view3d.
    /* zoomfactor 2^(1/4): 4 scroll wheel clicks to double in size. */
    constexpr float zoomfactor = 1.1892;
    if (_yoffset != 0) {
        float z = pow(zoomfactor, _yoffset);

        // zoom view2d
        glm::vec2 zoom = glm::vec2(z,z);
        glm::vec2 origin = {getWindowWidth()/2, getWindowHeight()/2};
        m_view2d = glm::translate(m_view2d, origin);
        m_view2d = glm::scale(m_view2d, zoom);
        m_view2d = glm::translate(m_view2d, -origin);
        
        flagChange();
    }
}

void Sandbox::onMouseDrag(float _x, float _y, int _button) {
    if (_button == 1) {
        // Left-button drag is used to pan u_view2d.
        m_view2d = glm::translate(m_view2d, -getMouseVelocity());

        // Left-button drag is used to rotate geometry.
        float dist = uniforms.getCamera().getDistance();

        float vel_x = getMouseVelX();
        float vel_y = getMouseVelY();

        if (fabs(vel_x) < 50.0 && fabs(vel_y) < 50.0) {
            m_lat -= vel_x;
            m_lon -= vel_y * 0.5;
            uniforms.getCamera().orbit(m_lat, m_lon, dist);
            uniforms.getCamera().lookAt(glm::vec3(0.0));
        }
    } 
    else {
        // Right-button drag is used to zoom geometry.
        float dist = uniforms.getCamera().getDistance();
        dist += (-.008f * getMouseVelY());
        if (dist > 0.0f) {
            uniforms.getCamera().setDistance( dist );
        }
    }

    // flagChange();
}

void Sandbox::onViewportResize(int _newWidth, int _newHeight) {
    uniforms.getCamera().setViewport(_newWidth, _newHeight);
    
    for (unsigned int i = 0; i < uniforms.buffers.size(); i++) 
        uniforms.buffers[i].allocate(_newWidth, _newHeight, COLOR_TEXTURE);

    if (m_postprocessing || m_histogram)
        m_scene_fbo.allocate(_newWidth, _newHeight, uniforms.functions["u_sceneDepth"].present ? COLOR_DEPTH_TEXTURES : COLOR_TEXTURE_DEPTH_BUFFER);

    if (m_record || screenshotFile != "")
        m_record_fbo.allocate(_newWidth, _newHeight, COLOR_TEXTURE_DEPTH_BUFFER);

    flagChange();
}

void Sandbox::onScreenshot(std::string _file) {
    if (_file != "" && isGL()) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_record_fbo.getId());

        unsigned char* pixels = new unsigned char[getWindowWidth() * getWindowHeight()*4];
        glReadPixels(0, 0, getWindowWidth(), getWindowHeight(), GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        savePixels(_file, pixels, getWindowWidth(), getWindowHeight());
        delete[] pixels;

        if (!m_record) {
            std::cout << "// Screenshot saved to " << _file << std::endl;
            std::cout << "// > ";
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

void Sandbox::onHistogram() {
    if ( isGL() && haveChange() ) {

        // Extract pixels
        glBindFramebuffer(GL_FRAMEBUFFER, m_scene_fbo.getId());
        int w = getWindowWidth();
        int h = getWindowHeight();
        int c = 4;
        int total = w * h * c;
        unsigned char* pixels = new unsigned char[total];
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Count frequencies of appearances 
        float max_rgb_freq = 0;
        float max_luma_freq = 0;
        glm::vec4 freqs[256];
        for (int i = 0; i < total; i += c) {
            freqs[pixels[i]].r++;
            if (freqs[pixels[i]].r > max_rgb_freq)
                max_rgb_freq = freqs[pixels[i]].r;

            freqs[pixels[i+1]].g++;
            if (freqs[pixels[i+1]].g > max_rgb_freq)
                max_rgb_freq = freqs[pixels[i+1]].g;

            freqs[pixels[i+2]].b++;
            if (freqs[pixels[i+2]].b > max_rgb_freq)
                max_rgb_freq = freqs[pixels[i+2]].b;

            int luma = 0.299 * pixels[i] + 0.587 * pixels[i+1] + 0.114 * pixels[i+2];
            freqs[luma].a++;
            if (freqs[luma].a > max_luma_freq)
                max_luma_freq = freqs[luma].a;
        }
        delete[] pixels;

        // Normalize frequencies
        for (int i = 0; i < 255; i ++)
            freqs[i] = freqs[i] / glm::vec4(max_rgb_freq, max_rgb_freq, max_rgb_freq, max_luma_freq);

        if (m_histogram_texture == nullptr)
            m_histogram_texture = new Texture();

        m_histogram_texture->load(256, 1, 4, 32, &freqs[0]);

        uniforms.textures["u_sceneHistogram"] = m_histogram_texture;
    }
}

