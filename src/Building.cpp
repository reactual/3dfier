/*
  3dfier: takes 2D GIS datasets and "3dfies" to create 3D city models.

  Copyright (C) 2015-2018  3D geoinformation research group, TU Delft

  This file is part of 3dfier.

  3dfier is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  3dfier is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with 3difer.  If not, see <http://www.gnu.org/licenses/>.

  For any information or further details about the use of 3dfier, contact
  Hugo Ledoux
  <h.ledoux@tudelft.nl>
  Faculty of Architecture & the Built Environment
  Delft University of Technology
  Julianalaan 134, Delft 2628BL, the Netherlands
*/

#include "Building.h"
#include "io.h"
#include <algorithm>

//-- static variable
float Building::_heightref_top;
float Building::_heightref_base;
bool Building::_building_triangulate;
bool Building::_building_include_floor;
bool Building::_building_inner_walls;
std::set<int> Building::_las_classes_roof;
std::set<int> Building::_las_classes_ground;
Building::Building(char *wkt, std::string layername, AttributeMap attributes, std::string pid, float heightref_top, float heightref_base, bool building_triangulate, bool building_include_floor, bool building_inner_walls)
  : Flat(wkt, layername, attributes, pid)
{
  _heightref_top = heightref_top;
  _heightref_base = heightref_base;
  _building_triangulate = building_triangulate;
  _building_include_floor = building_include_floor;
  _building_inner_walls = building_inner_walls;
}

void Building::set_las_classes_roof(std::set<int> theset)
{
  Building::_las_classes_roof = theset;
}

void Building::set_las_classes_ground(std::set<int> theset)
{
  Building::_las_classes_ground = theset;
}

std::string Building::get_all_z_values() {
  std::vector<int> allz (_zvaluesground.begin(), _zvaluesground.end());
  allz.insert(allz.end(), _zvaluesinside.begin(), _zvaluesinside.end());
  std::sort(allz.begin(), allz.end());
  std::stringstream ss;
  for (auto& z : allz)
    ss << z / 100.0 << "|";
  return ss.str();
}

int Building::get_height_ground_at_percentile(float percentile) {
  if (_zvaluesground.empty() == false) {
    std::nth_element(_zvaluesground.begin(), _zvaluesground.begin() + (_zvaluesground.size() * percentile), _zvaluesground.end());
    return _zvaluesground[_zvaluesground.size() * percentile];
  }
  else {
    return -9999;
  }
}

int Building::get_height_roof_at_percentile(float percentile) {
  if (_zvaluesinside.empty() == false) {
    std::nth_element(_zvaluesinside.begin(), _zvaluesinside.begin() + (_zvaluesinside.size() * percentile), _zvaluesinside.end());
    return _zvaluesinside[_zvaluesinside.size() * percentile];
  }
  else {
    return -9999;
  }
}

int Building::get_RMSE() {
  if (_distancesinside.empty() == false) {
    //-- B: just return the nr. of points inside for now
    std::vector<int> squared;
    int sum = 0;
    for (int& d : _distancesinside) squared.push_back(d*d);
    for (auto& s : squared) sum += s;
    int n = _distancesinside.size();
    return sqrt(sum/n);
  }
  else {
    return -9999.0;
  }
}

bool Building::lift() {
  //-- for the ground
  if (_zvaluesground.empty() == false) {
    //-- Only use ground points for base height calculation
    std::nth_element(_zvaluesground.begin(), _zvaluesground.begin() + (_zvaluesground.size() * _heightref_base), _zvaluesground.end());
    _height_base = _zvaluesground[_zvaluesground.size() * _heightref_base];
  }
  else if (_zvaluesinside.empty() == false) {
    std::nth_element(_zvaluesinside.begin(), _zvaluesinside.begin() + (_zvaluesinside.size() * _heightref_base), _zvaluesinside.end());
    _height_base = _zvaluesinside[_zvaluesinside.size() * _heightref_base];
  }
  else {
    _height_base = -9999;
  }
  //-- for the roof
  Flat::lift_percentile(_heightref_top);
  return true;
}

bool Building::add_elevation_point(Point2 &p, double z, float radius, int lasclass) {
  if (within_range(p, *(_p2), radius)) {
    int zcm = int(z * 100);
    if ( (_las_classes_roof.empty() == true) || (_las_classes_roof.count(lasclass) > 0) ) {
      _zvaluesinside.push_back(zcm);
    }
    if ( (_las_classes_ground.empty() == true) || (_las_classes_ground.count(lasclass) > 0) ) {
      _zvaluesground.push_back(zcm);
    }
  }
  return true;
}

bool Building::add_point_distance(liblas::Point const& laspt, float radius,
                                  AABB_Tree const& TriTree) {
  int lasclass = laspt.GetClassification().GetClass();
  Point2 p(laspt.GetX(), laspt.GetY());
  if (within_range(p, *(_p2), radius)) {
    //-- B: distance_3d(AABB tree, laspt) here, and add only the distance as integer [cm]
    /* distance_3d(AABB tree, laspt) returns a double, multiply that by 100
     * and store the integer instead of the double. Then when accessing the
     * values (eg. for RMSE), use the integers for computations, BUT
     * at the end don't forget to divide and convert to double again
     */
    double dist = distance_3d(TriTree, laspt);
    if ( (_las_classes_roof.empty() == true) || (_las_classes_roof.count(lasclass) > 0) ) {
      _distancesinside.push_back(dist * 100); //-- to cm
    }
  }
  return true;
}

void Building::construct_building_walls(const NodeColumn& nc) {
  //-- gather all rings
  std::vector<Ring2> therings;
  therings.push_back(_p2->outer());
  for (Ring2& iring : _p2->inners())
    therings.push_back(iring);

  //-- process each vertex of the polygon separately
  Point2 a, b;
  TopoFeature* fadj;
  int ringi = -1;
  for (Ring2& ring : therings) {
    ringi++;
    for (int ai = 0; ai < ring.size(); ai++) {
      //-- Point a
      a = ring[ai];
      //-- find Point b
      int bi;
      if (ai == (ring.size() - 1)) {
        b = ring.front();
        bi = 0;
      }
      else {
        b = ring[ai + 1];
        bi = ai + 1;
      }

      //-- find the adjacent polygon to segment ab (fadj)
      fadj = nullptr;
      int adj_a_ringi = 0;
      int adj_a_pi = 0;
      int adj_b_ringi = 0;
      int adj_b_pi = 0;
      for (auto& adj : *(_adjFeatures)) {
        if (adj->has_segment(b, a, adj_b_ringi, adj_b_pi, adj_a_ringi, adj_a_pi)) {
          fadj = adj;
          break;
        }
      }

      std::unordered_map<std::string, std::vector<int>>::const_iterator ncit;
      std::vector<int> anc, bnc;
      //-- check if there's a nc for either
      ncit = nc.find(gen_key_bucket(&a));
      if (ncit != nc.end()) {
        anc = ncit->second;
      }
      ncit = nc.find(gen_key_bucket(&b));
      if (ncit != nc.end()) {
        bnc = ncit->second;
      }

      if (anc.empty() && bnc.empty())
        continue;
      // if one of the nc is empty something is wrong
      if (anc.empty() || bnc.empty()) {
        std::cerr << "ERROR: the inner wall node column is empty for building:  " << _id << std::endl;
        break;
      }

      std::vector<int> awall, bwall, awallend, bwallend;
      //-- find the height of the vertex in the node column
      std::vector<int>::const_iterator sait, eait, sbit, ebit;
      int roofheight = this->get_vertex_elevation(ringi, ai);
      int baseheight = this->get_height_base();
      if (fadj == nullptr || fadj->get_class() != BUILDING) {
        // start at adjacent height for correct stitching if no floor
        if (fadj == nullptr ||_building_include_floor) {
          awall.push_back(baseheight);
          bwall.push_back(baseheight);
        }
        else {
          awall.push_back(fadj->get_vertex_elevation(adj_a_ringi, adj_a_pi));
          bwall.push_back(fadj->get_vertex_elevation(adj_b_ringi, adj_b_pi));
        }
        // end at own roof height
        awallend.push_back(roofheight);
        bwallend.push_back(roofheight);
      }
      else { // case of shared wall between two connected buildings
        int adjbaseheight = dynamic_cast<Building*>(fadj)->get_height_base();
        int adjroofheight = fadj->get_vertex_elevation(adj_a_ringi, adj_a_pi);
        int base = baseheight;
        if (_building_include_floor && baseheight < adjbaseheight) {
          awall.push_back(baseheight);
          awallend.push_back(adjbaseheight);
          //store base for inner walls check
          base = adjbaseheight;
        }

        if (_building_inner_walls) {
          awall.push_back(base);
          if (roofheight > adjroofheight) {
            awallend.push_back(adjroofheight);
          }
          else {
            awallend.push_back(roofheight);
          }

        }
        if (roofheight > adjroofheight) {
          awall.push_back(adjroofheight);
          awallend.push_back(roofheight);
        }
        bwall = awall;
        bwallend = awallend;
      }

      for (int i = 0; i < awall.size(); i++) {
        sait = std::find(anc.begin(), anc.end(), awall[i]);
        sbit = std::find(bnc.begin(), bnc.end(), bwall[i]);
        eait = std::find(anc.begin(), anc.end(), awallend[i]);
        ebit = std::find(bnc.begin(), bnc.end(), bwallend[i]);

        //-- iterate to triangulate
        while (sbit != ebit && sbit != bnc.end() && sbit + 1 != bnc.end()) {
          Point3 p;
          p = Point3(bg::get<0>(a), bg::get<1>(a), z_to_float(*sait));
          _vertices_vw.push_back(std::make_pair(p, gen_key_bucket(&p)));
          p = Point3(bg::get<0>(b), bg::get<1>(b), z_to_float(*sbit));
          _vertices_vw.push_back(std::make_pair(p, gen_key_bucket(&p)));
          sbit++;
          p = Point3(bg::get<0>(b), bg::get<1>(b), z_to_float(*sbit));
          _vertices_vw.push_back(std::make_pair(p, gen_key_bucket(&p)));
          Triangle t;
          int size = int(_vertices_vw.size());
          t.v0 = size - 2;
          t.v1 = size - 3;
          t.v2 = size - 1;
          _triangles_vw.push_back(t);
        }
        while (sait != eait && sait != anc.end() && sait + 1 != anc.end()) {
          Point3 p;
          p = Point3(bg::get<0>(b), bg::get<1>(b), z_to_float(*ebit));
          _vertices_vw.push_back(std::make_pair(p, gen_key_bucket(&p)));
          p = Point3(bg::get<0>(a), bg::get<1>(a), z_to_float(*sait));
          _vertices_vw.push_back(std::make_pair(p, gen_key_bucket(&p)));
          sait++;
          p = Point3(bg::get<0>(a), bg::get<1>(a), z_to_float(*sait));
          _vertices_vw.push_back(std::make_pair(p, gen_key_bucket(&p)));
          Triangle t;
          int size = int(_vertices_vw.size());
          t.v0 = size - 3;
          t.v1 = size - 2;
          t.v2 = size - 1;
          _triangles_vw.push_back(t);
        }
      }
    }
  }
}

int Building::get_height_base() {
  return _height_base;
}

TopoClass Building::get_class() {
  return BUILDING;
}

bool Building::is_hard() {
  return true;
}

void Building::get_csv(std::wostream& of) {
  of << this->get_id() << ";" <<
    std::setprecision(2) << std::fixed <<
    this->get_height_roof_at_percentile(_heightref_top) / 100.0 << ";" <<
    this->get_height_ground_at_percentile(_heightref_base) / 100.0 << "\n";
}

std::string Building::get_mtl() {
  return "usemtl Building";
}

void Building::get_obj(std::unordered_map< std::string, unsigned long > &dPts, int lod, std::string mtl, std::string &fs) {
  if (lod == 1) {
    TopoFeature::get_obj(dPts, mtl, fs);
  }
  else if (lod == 0) {
    fs += mtl;
    fs += "\n";
    for (auto& t : _triangles) {
      unsigned long a, b, c;
      float z = z_to_float(this->get_height_base());
      auto it = dPts.find(gen_key_bucket(&_vertices[t.v0].first, z));
      if (it == dPts.end()) {
        a = dPts.size() + 1;
        dPts[gen_key_bucket(&_vertices[t.v0].first, z)] = a;
      }
      else {
        a = it->second;
      }
      it = dPts.find(gen_key_bucket(&_vertices[t.v1].first, z));
      if (it == dPts.end()) {
        b = dPts.size() + 1;
        dPts[gen_key_bucket(&_vertices[t.v1].first, z)] = b;
      }
      else {
        b = it->second;
      }
      it = dPts.find(gen_key_bucket(&_vertices[t.v2].first, z));
      if (it == dPts.end()) {
        c = dPts.size() + 1;
        dPts[gen_key_bucket(&_vertices[t.v2].first, z)] = c;
      }
      else {
        c = it->second;
      }
      if ((a != b) && (a != c) && (b != c)) {
        fs += "f "; fs += std::to_string(a); fs += " "; fs += std::to_string(b); fs += " "; fs += std::to_string(c); fs += "\n";
      }
    }
  }
  if (_building_include_floor) {
    fs += "usemtl BuildingFloor\n";
    for (auto& t : _triangles) {
      unsigned long a, b, c;
      float z = z_to_float(this->get_height_base());
      auto it = dPts.find(gen_key_bucket(&_vertices[t.v0].first, z));
      if (it == dPts.end()) {
        a = dPts.size() + 1;
        dPts[gen_key_bucket(&_vertices[t.v0].first, z)] = a;
      }
      else {
        a = it->second;
      }
      it = dPts.find(gen_key_bucket(&_vertices[t.v1].first, z));
      if (it == dPts.end()) {
        b = dPts.size() + 1;
        dPts[gen_key_bucket(&_vertices[t.v1].first, z)] = b;
      }
      else {
        b = it->second;
      }
      it = dPts.find(gen_key_bucket(&_vertices[t.v2].first, z));
      if (it == dPts.end()) {
        c = dPts.size() + 1;
        dPts[gen_key_bucket(&_vertices[t.v2].first, z)] = c;
      }
      else {
        c = it->second;
      }
      //reverse orientation for floor polygon, a-c-b instead of a-b-c.
      if ((a != b) && (a != c) && (b != c)) {
        fs += "f "; fs += std::to_string(a); fs += " "; fs += std::to_string(c); fs += " "; fs += std::to_string(b); fs += "\n";
      }
    }
  }
}

void Building::get_cityjson(nlohmann::json& j, std::unordered_map<std::string, unsigned long> &dPts) {
  nlohmann::json b;
  b["type"] = "Building";
  b["attributes"];
  get_cityjson_attributes(b, _attributes);
  float hbase = z_to_float(this->get_height_base());
  float h = z_to_float(this->get_height());
  b["attributes"]["min-height-surface"] = hbase;
  b["attributes"]["measuredHeight"] = h;
  nlohmann::json g;
  this->get_cityjson_geom(g, dPts, "Solid");
  b["geometry"].push_back(g);
  j["CityObjects"][this->get_id()] = b;
}

void Building::get_citygml(std::wostream& of) {
  float h = z_to_float(this->get_height());
  float hbase = z_to_float(this->get_height_base());
  of << "<cityObjectMember>";
  of << "<bui:Building gml:id=\"" << this->get_id() << "\">";
  get_citygml_attributes(of, _attributes);
  of << "<gen:measureAttribute name=\"min height surface\">";
  of << "<gen:value uom=\"#m\">" << hbase << "</gen:value>";
  of << "</gen:measureAttribute>";
  of << "<bui:measuredHeight uom=\"#m\">" << h << "</bui:measuredHeight>";
  //-- LOD0 footprint
  of << "<bui:lod0FootPrint>";
  of << "<gml:MultiSurface>";
  get_polygon_lifted_gml(of, this->_p2, hbase, true);
  of << "</gml:MultiSurface>";
  of << "</bui:lod0FootPrint>";
  //-- LOD0 roofedge
  of << "<bui:lod0RoofEdge>";
  of << "<gml:MultiSurface>";
  get_polygon_lifted_gml(of, this->_p2, h, true);
  of << "</gml:MultiSurface>";
  of << "</bui:lod0RoofEdge>";
  //-- LOD1 Solid
  of << "<bui:lod1Solid>";
  of << "<gml:Solid>";
  of << "<gml:exterior>";
  of << "<gml:CompositeSurface>";
  if (_building_triangulate) {
    for (auto& t : _triangles)
      get_triangle_as_gml_surfacemember(of, t);
    for (auto& t : _triangles_vw)
      get_triangle_as_gml_surfacemember(of, t, true);
    if (_building_include_floor) {
      for (auto& t : _triangles) {
        get_floor_triangle_as_gml_surfacemember(of, t, _height_base);
      }
    }
  }
  else {
    get_extruded_lod1_block_gml(of, this->_p2, h, hbase, _building_include_floor);
  }
  of << "</gml:CompositeSurface>";
  of << "</gml:exterior>";
  of << "</gml:Solid>";
  of << "</bui:lod1Solid>";
  of << "</bui:Building>";
  of << "</cityObjectMember>";
}

void Building::get_citygml_imgeo(std::wostream& of) {
  float h = z_to_float(this->get_height());
  float hbase = z_to_float(this->get_height_base());
  of << "<cityObjectMember>";
  of << "<bui:Building gml:id=\"" << this->get_id() << "\">";
  //-- store building information
  get_imgeo_object_info(of, this->get_id());
  of << "<bui:consistsOfBuildingPart>";
  of << "<bui:BuildingPart>";
  //-- LOD1 Solid
  of << "<bui:lod1Solid>";
  of << "<gml:Solid>";
  of << "<gml:exterior>";
  of << "<gml:CompositeSurface>";
  if (_building_triangulate) {
    for (auto& t : _triangles)
      get_triangle_as_gml_surfacemember(of, t);
    for (auto& t : _triangles_vw)
      get_triangle_as_gml_surfacemember(of, t, true);
    if (_building_include_floor) {
      for (auto& t : _triangles) {
        get_floor_triangle_as_gml_surfacemember(of, t, _height_base);
      }
    }
  }
  else {
    if (_building_include_floor) {
      //-- get floor
      get_polygon_lifted_gml(of, this->_p2, hbase, false);
    }
    //-- get roof
    get_polygon_lifted_gml(of, this->_p2, h, true);
    //-- get the walls
    auto r = _p2->outer();
    int i;
    for (i = 0; i < (r.size() - 1); i++)
      get_extruded_line_gml(of, &r[i], &r[i + 1], h, hbase, false);
    get_extruded_line_gml(of, &r[i], &r[0], h, hbase, false);
    //-- irings
    auto irings = _p2->inners();
    for (Ring2& r : irings) {
      for (i = 0; i < (r.size() - 1); i++)
        get_extruded_line_gml(of, &r[i], &r[i + 1], h, hbase, false);
      get_extruded_line_gml(of, &r[i], &r[0], h, hbase, false);
    }
  }
  of << "</gml:CompositeSurface>";
  of << "</gml:exterior>";
  of << "</gml:Solid>";
  of << "</bui:lod1Solid>";
  std::string attribute;
  if (get_attribute("identificatiebagpnd", attribute)) {
    of << "<imgeo:identificatieBAGPND>" << attribute << "</imgeo:identificatieBAGPND>";
  }
  get_imgeo_nummeraanduiding(of);
  of << "</bui:BuildingPart>";
  of << "</bui:consistsOfBuildingPart>";
  of << "</bui:Building>";
  of << "</cityObjectMember>";
}

void Building::get_imgeo_nummeraanduiding(std::wostream& of) {
  std::string attribute;
  bool btekst, bplaatsingspunt, bhoek, blaagnr, bhoognr;
  std::string tekst, plaatsingspunt, hoek, laagnr, hoognr;
  btekst = get_attribute("tekst", tekst);
  bplaatsingspunt = get_attribute("plaatsingspunt", plaatsingspunt);
  bhoek = get_attribute("hoek", hoek);
  blaagnr = get_attribute("identificatiebagvbolaagstehuisnummer", laagnr);
  bhoognr = get_attribute("identificatiebagvbohoogstehuisnummer", hoognr);

  if (btekst) {
    // Split the lists into vector of strings
    std::vector<std::string> tekst_split, plaatsingspunt_split, hoek_split, laagnr_split, hoognr_split;
    tekst_split = stringsplit(tekst.substr(3, tekst.size() - 4), ',');
    if (bplaatsingspunt) {
      plaatsingspunt_split = stringsplit(plaatsingspunt.substr(3, plaatsingspunt.size() - 4), ',');
    }
    if (bhoek) {
      hoek_split = stringsplit(hoek.substr(3, hoek.size() - 4), ',');
    }
    if (blaagnr) {
      laagnr_split = stringsplit(laagnr.substr(3, laagnr.size() - 4), ',');
    }
    if (bhoognr) {
      hoognr_split = stringsplit(hoognr.substr(3, hoognr.size() - 4), ',');
    }

    //Get the amount of text in the StringList and write all List values separate
    int count = boost::lexical_cast<int>(tekst[1]);
    for (int i = 0; i < count; i++) {
      if (i < tekst_split.size() && i < plaatsingspunt_split.size() && i < hoek_split.size()) {
        of << "<imgeo:nummeraanduidingreeks>";
        of << "<imgeo:Nummeraanduidingreeks>";
        of << "<imgeo:nummeraanduidingreeks>";
        of << "<imgeo:Label>";
        of << "<imgeo:tekst>" << tekst_split.at(i) << "</imgeo:tekst>";
        of << "<imgeo:positie>";
        of << "<imgeo:Labelpositie>";
        of << "<imgeo:plaatsingspunt><gml:Point srsDimension=\"2\"><gml:pos>" << plaatsingspunt_split.at(i) << "</gml:pos></gml:Point></imgeo:plaatsingspunt>";
        of << "<imgeo:hoek>" << hoek_split.at(i) << "</imgeo:hoek>";
        of << "</imgeo:Labelpositie>";
        of << "</imgeo:positie>";
        of << "</imgeo:Label>";
        of << "</imgeo:nummeraanduidingreeks>";
        if (i < laagnr_split.size()) {
          of << "<imgeo:identificatieBAGVBOLaagsteHuisnummer>" << laagnr_split.at(i) << "</imgeo:identificatieBAGVBOLaagsteHuisnummer>";
        }
        if (i < hoognr_split.size()) {
          of << "<imgeo:identificatieBAGVBOHoogsteHuisnummer>" << hoognr_split.at(i) << "</imgeo:identificatieBAGVBOHoogsteHuisnummer>";
        }
        of << "</imgeo:Nummeraanduidingreeks>";
        of << "</imgeo:nummeraanduidingreeks>";
      }
    }
  }
}

bool Building::get_shape(OGRLayer* layer, bool writeAttributes, AttributeMap extraAttributes) {
  return TopoFeature::get_multipolygon_features(layer, "Building", writeAttributes, extraAttributes, true, this->get_height_base(), this->get_height());
}
