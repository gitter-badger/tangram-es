#include "textBatch.h"

#include "textStyle.h"
#include "text/textBuffer.h"
#include "text/fontContext.h"
#include "view/view.h"

TextBatch::TextBatch(const TextStyle& _style)
    : m_fontContext(_style.m_labels->getFontContext()),
      m_mesh(std::make_shared<TypedMesh<BufferVert>>(_style.m_vertexLayout, GL_TRIANGLES, GL_DYNAMIC_DRAW)),
      m_style(_style)
{
    m_dirtyTransform = false;
}

void TextBatch::init() {
    m_fontContext->lock();
    glfonsBufferCreate(m_fontContext->getFontContext(), &m_fsBuffer);
    m_fontContext->unlock();
}

TextBatch::~TextBatch() {
    glfonsBufferDelete(m_fontContext->getFontContext(), m_fsBuffer);
}

int TextBatch::getVerticesSize() {
    auto ctx = m_fontContext->bind(m_fsBuffer);
    return glfonsVerticesSize(ctx.get());
}

fsuint TextBatch::genTextID() {
    fsuint id;
    auto ctx = m_fontContext->bind(m_fsBuffer);
    glfonsGenText(ctx.get(), 1, &id);
    return id;
}

bool TextBatch::rasterize(const std::string& _text, fsuint _id) {
    auto ctx = m_fontContext->bind(m_fsBuffer);
    return glfonsRasterize(ctx.get(), _id, _text.c_str()) == GLFONS_VALID;
}

void TextBatch::pushBuffer() {
    if (m_dirtyTransform) {

        auto ctx = m_fontContext->bind(m_fsBuffer);
        glfonsUpdateBuffer(ctx.get(), m_mesh.get());

        m_dirtyTransform = false;
    }
}

void TextBatch::transformID(fsuint _textID, const BufferVert::State& _state) {
    auto ctx = m_fontContext->bind(m_fsBuffer);
    glfonsTransform(ctx.get(), _textID,
                    _state.screenPos.x, _state.screenPos.y,
                    _state.rotation, _state.alpha);

    m_dirtyTransform = true;
}

glm::vec4 TextBatch::getBBox(fsuint _textID) {
    glm::vec4 bbox;
    auto ctx = m_fontContext->bind(m_fsBuffer);
    glfonsGetBBox(ctx.get(), _textID, &bbox.x, &bbox.y, &bbox.z, &bbox.w);

    return bbox;
}

void TextBatch::add(const Feature& _feature, const StyleParamMap& _params, const MapTile& _tile) {

    switch (_feature.geometryType) {
        case GeometryType::points:
            for (auto& point : _feature.points) {
                buildPoint(point, _feature.props, _tile);
            }
            break;
        case GeometryType::lines:
            for (auto& line : _feature.lines) {
                buildLine(line, _feature.props, _tile);
            }
            break;
        case GeometryType::polygons:
            for (auto& polygon : _feature.polygons) {
                buildPolygon(polygon, _feature.props, _tile);
            }
            break;
        default:
            break;
    }
}

void TextBatch::buildPoint(const Point& _point, const Properties& _props, const MapTile& _tile) {

    std::string name;

    if (_props.getString("name", name)) {

        addLabel(m_style.m_labels->addTextLabel(*this, _tile, { glm::vec2(_point), glm::vec2(_point) },
                                                name, Label::Type::point));
    }
}

void TextBatch::buildLine(const Line& _line, const Properties& _props, const MapTile& _tile) {

    std::string name;

    if (_props.getString("name", name)) {

        int lineLength = _line.size();
        int skipOffset = floor(lineLength / 2);
        float minLength = 0.15; // default, probably need some more thoughts


        for (size_t i = 0; i < _line.size() - 1; i += skipOffset) {
            glm::vec2 p1 = glm::vec2(_line[i]);
            glm::vec2 p2 = glm::vec2(_line[i + 1]);

            glm::vec2 p1p2 = p2 - p1;
            float length = glm::length(p1p2);

            if (length < minLength) {
                continue;
            }

            addLabel(m_style.m_labels->addTextLabel(*this, _tile, { p1, p2 }, name, Label::Type::line));
        }
    }
}

void TextBatch::buildPolygon(const Polygon& _polygon, const Properties& _props, const MapTile& _tile) {

    std::string name;

    if (_props.getString("name", name)) {

        glm::vec3 centroid;
        int n = 0;

        for (auto& l : _polygon) {
            for (auto& p : l) {
                centroid.x += p.x;
                centroid.y += p.y;
                n++;
            }
        }

        centroid /= n;

        addLabel(m_style.m_labels->addTextLabel(*this, _tile, { glm::vec2(centroid), glm::vec2(centroid) },
                                                name, Label::Type::point));
    }
}

bool TextBatch::compile() {

    std::vector<BufferVert> vertices;
    int bufferSize = getVerticesSize();

    if (bufferSize == 0) {
        return false;
    }
    vertices.resize(bufferSize);

    /* get the vertices from the font context and add them as vbo mesh data */
    bool res;
    {
        auto ctx = m_fontContext->bind(m_fsBuffer);
        res = glfonsVertices(ctx.get(), reinterpret_cast<float*>(vertices.data()));
    }
    if (res) {
        m_mesh->addVertices(std::move(vertices), {});
    }

    if (m_mesh->numVertices() > 0) {
        m_mesh->compileVertexBuffer();
        return true;
    }
    return false;
};


void TextBatch::draw(const View& _view) {
    auto shader =  m_style.getShaderProgram();

    shader->setUniformf("u_color", 0.2, 0.2, 0.2);
    shader->setUniformf("u_sdf", 0.3);

    m_mesh->draw(shader);

    float r = (m_style.m_color >> 16 & 0xff) / 255.0;
    float g = (m_style.m_color >> 8  & 0xff) / 255.0;
    float b = (m_style.m_color       & 0xff) / 255.0;

    shader->setUniformf("u_color", r, g, b);
    shader->setUniformf("u_sdf", 0.8);

    m_mesh->draw(shader);
};


void TextBatch::update(const glm::mat4& mvp, const View& _view, float _dt) {
    glm::vec2 screenSize = glm::vec2(_view.getWidth(), _view.getHeight());
    for (auto& label : m_labels) {
        label->update(mvp, screenSize, _dt);
    }
}

void TextBatch::prepare() {
    for(auto& label : m_labels) {
        label->pushTransform(*this);
    }
    pushBuffer();
}