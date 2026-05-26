#include "obj_loader.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <filesystem>
#include <limits>

static bool load_mtl(const std::string& mtl_path, std::vector<ObjMaterial>& out_mats){
    std::ifstream f(mtl_path);
    if (!f.is_open()){
        std::cerr << "[obj] cannot open mtl: " << mtl_path << "\n";
        return false;
    }

    ObjMaterial* cur = nullptr;
    std::string line;

    while (std::getline(f, line)){
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "newmtl"){
            out_mats.push_back({});
            cur = &out_mats.back();
            ss >> cur->name;
        }
        else if (token == "Kd" && cur){
            ss >> cur->kd.r >> cur->kd.g >> cur->kd.b;
        }
        else if (token == "map_Kd" && cur){
            std::string rel_path;
            ss >> rel_path;
            std::filesystem::path tex_name = std::filesystem::path(rel_path).filename();
            std::filesystem::path mtl_dir = std::filesystem::path(mtl_path).parent_path();
            std::filesystem::path local = mtl_dir / tex_name;
            if (std::filesystem::exists(local)){
                cur->tex_path = std::filesystem::weakly_canonical(local).string();
                std::cout << "[mtl] tex found: " << cur->tex_path << "\n";
            }
            else{
                std::cout << "[mtl] tex missing (copy to assets/): " << tex_name.string() << "\n";
                cur->tex_path = "";
            }
        }
    }

    std::cout << "[obj] loaded " << out_mats.size() << " materials from " << mtl_path << "\n";
    return true;
}

bool obj_load(const std::string& obj_path, ObjData& out){
    std::ifstream f(obj_path);
    if (!f.is_open()){ std::cerr << "[obj] cannot open: " << obj_path << "\n";
        return false;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;

    struct FaceVert { int pi, ti, ni; };
    std::unordered_map<int64_t, int> cache;

    auto pack_key = [](int pi, int ti, int ni) -> int64_t {
        return (int64_t)pi * 1000000000LL + (int64_t)ti * 1000000LL + ni;
    };

    // per-part, per-material sub-buffer built during parse
    struct SubBuf {
        std::vector<float> verts;
        // bounds for pivot computation
        glm::vec3 bmin = glm::vec3( 1e30f);
        glm::vec3 bmax = glm::vec3(-1e30f);
    };
    // part_name -> mat_name -> SubBuf
    std::unordered_map<std::string, std::unordered_map<std::string, SubBuf>> part_bufs;

    std::vector<std::string> part_order;
    std::unordered_map<std::string, std::vector<std::string>> part_mat_order;

    std::string cur_part = "__default__";
    std::string cur_mat  = "";
    part_order.push_back(cur_part);

    std::string line;

    while (std::getline(f, line)){
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "mtllib"){
            std::string mtl_file;
            ss >> mtl_file;
            std::filesystem::path dir = std::filesystem::path(obj_path).parent_path();
            load_mtl((dir / mtl_file).string(), out.materials);
        }
        else if (token == "v"){
            glm::vec3 p;
            ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        else if (token == "vn"){
            glm::vec3 n;
            ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (token == "vt"){
            glm::vec2 uv;
            ss >> uv.x >> uv.y;
            texcoords.push_back(uv);
        }
        // g and o both treated as part boundaries
        else if (token == "g" || token == "o"){
            std::string name;
            ss >> name;
            if (name.empty() || name == "off") continue;
            if (part_bufs.find(name) == part_bufs.end())
                part_order.push_back(name);
            cur_part = name;
        }
        else if (token == "usemtl"){
            ss >> cur_mat;
            // track mat insertion order per part
            auto& mord = part_mat_order[cur_part];
            bool found = false;
            for (auto& m : mord) if (m == cur_mat) { found = true; break; }
            if (!found) mord.push_back(cur_mat);
        }
        else if (token == "f"){
            std::vector<FaceVert> fv;
            std::string vt;
            while (ss >> vt){
                int raw_pi = 0, raw_ti = 0, raw_ni = 0;
                size_t s1 = vt.find('/');
                size_t s2 = vt.rfind('/');
                raw_pi = std::stoi(vt.substr(0, s1));
                if (s1 != std::string::npos && s1 + 1 < vt.size() && s1 != s2)
                    raw_ti = std::stoi(vt.substr(s1 + 1, s2 - s1 - 1));
                if (s2 != std::string::npos && s2 + 1 < vt.size())
                    raw_ni = std::stoi(vt.substr(s2 + 1));
                int pi = (raw_pi < 0) ? (int)positions.size()  + raw_pi : raw_pi - 1;
                int ti = (raw_ti < 0) ? (int)texcoords.size()  + raw_ti : raw_ti - 1;
                int ni = (raw_ni < 0) ? (int)normals.size()    + raw_ni : raw_ni - 1;
                fv.push_back({pi, ti, ni});
            }
            if (fv.size() < 3) continue;

            SubBuf& buf = part_bufs[cur_part][cur_mat];

            auto emit = [&](FaceVert f){
                glm::vec3 p = (f.pi >= 0 && f.pi < (int)positions.size())
                               ? positions[f.pi]  : glm::vec3(0);
                glm::vec3 n = (f.ni >= 0 && f.ni < (int)normals.size())
                               ? normals[f.ni]    : glm::vec3(0,1,0);
                glm::vec2 uv = (f.ti >= 0 && f.ti < (int)texcoords.size())
                               ? texcoords[f.ti]  : glm::vec2(0);
                n = glm::normalize(n);
                buf.verts.push_back(p.x);  buf.verts.push_back(p.y);  buf.verts.push_back(p.z);
                buf.verts.push_back(n.x);  buf.verts.push_back(n.y);  buf.verts.push_back(n.z);
                buf.verts.push_back(uv.x); buf.verts.push_back(uv.y);
                // expand bounds for pivot
                buf.bmin = glm::min(buf.bmin, p);
                buf.bmax = glm::max(buf.bmax, p);
            };

            for (size_t i = 1; i + 1 < fv.size(); i++){
                emit(fv[0]);
                emit(fv[i]);
                emit(fv[i+1]);
            }
        }
    }

    // concatenate all sub-buffers into one flat buffer
    // build ObjPart list in file order
    for (const auto& pname : part_order){
        auto pit = part_bufs.find(pname);
        if (pit == part_bufs.end()) continue;

        ObjPart part;
        part.part_name = pname;

        glm::vec3 part_bmin( 1e30f);
        glm::vec3 part_bmax(-1e30f);

        // mats in insertion order
        const auto& mord = part_mat_order[pname];
        for (const auto& mname : mord){
            auto mit = pit->second.find(mname);
            if (mit == pit->second.end()) continue;
            SubBuf& sb = mit->second;
            if (sb.verts.empty()) continue;

            ObjGroup grp;
            grp.mat_name = mname;
            grp.vertex_start = (int)out.vertices.size() / 8;
            grp.vertex_count = (int)sb.verts.size() / 8;

            out.vertices.insert(out.vertices.end(), sb.verts.begin(), sb.verts.end());

            part_bmin = glm::min(part_bmin, sb.bmin);
            part_bmax = glm::max(part_bmax, sb.bmax);

            part.groups.push_back(grp);

            // legacy flat group list
            out.groups.push_back(grp);
        }

        // pivot = geometric center of part bounding box
        part.pivot_offset = (part_bmin + part_bmax) * 0.5f;

        out.parts.push_back(std::move(part));
    }

    std::cout << "[obj] loaded " << obj_path << "\n";
    std::cout << "[obj] " << positions.size() << " positions, "
              << out.vertices.size() / 8 / 3 << " triangles, "
              << out.parts.size() << " parts\n";
    for (const auto& p : out.parts)
        std::cout << "  part: " << p.part_name
                  << "  pivot=(" << p.pivot_offset.x << ","
                  << p.pivot_offset.y << "," << p.pivot_offset.z << ")"
                  << "  groups=" << p.groups.size() << "\n";

    return true;
}

const ObjMaterial* obj_find_material(const ObjData& data, const std::string& name){
    for (const auto& m : data.materials)
        if (m.name == name) return &m;
    return nullptr;
}

const ObjPart* obj_find_part(const ObjData& data, const std::string& name){
    for (const auto& p : data.parts)
        if (p.part_name == name) return &p;
    return nullptr;
}