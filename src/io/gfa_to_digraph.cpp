#include <utility>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <thread>
#include <mutex>
#include <functional>
#include <atomic>
#include <vector>
#include <functional>

#include "gfakluge.hpp"

#include "../graph/digraph.hpp"
#include "./io.hpp"
#include "handlegraph/types.hpp"

namespace hg = handlegraph;

namespace io {
/**
 * Count the number of lines of each type in a GFA file.
 *
 * @param[in] filename The GFA file to read.
 * @return A map from line type to number of lines of that type.
 */
std::map<char, uint64_t> gfa_line_counts(const char* filename) {
  int gfa_fd = -1;
  char* gfa_buf = nullptr;
  std::size_t gfa_filesize = gfak::mmap_open(filename, gfa_buf, gfa_fd);
  if (gfa_fd == -1) {
	std::cerr << "Couldn't open GFA file " << filename << "." << std::endl;
	exit(1);
  }
  std::string line;
  std::size_t i = 0;
  //bool seen_newline = true;
  std::map<char, uint64_t> counts;
  while (i < gfa_filesize) {
	if (i == 0 || gfa_buf[i-1] == '\n') { counts[gfa_buf[i]]++; }
	++i;
  }
  gfak::mmap_close(gfa_buf, gfa_fd, gfa_filesize);
  return counts;
}

// from odgi

//const std::size_t THREAD_COUNT = 4;

// TODO:
// - what about nodes without incoming or outgoing edges?
/**
 * Read a GFA file into a DiGraph.
 *
 *
 * @param[in] filename The GFA file to read.
 * @param[out] dg The DiGraph to read into.
 */
digraph::DiGraph gfa_to_digraph(const char* filename) {

  std::cout << "[io::gfa_to_digraph]" << "\n";
	
  gfak::GFAKluge gg = gfak::GFAKluge();

  std::cout << "he\n";
  gg.parse_gfa_file(filename);
  std::cout << "oe\n";
  /*
    Preprocess the GFA
    ------------------

	scan over the file to count edges and sequences in parallel
  */


  std::map<char, uint64_t> line_counts;
  std::size_t min_id = std::numeric_limits<uint64_t>::max();
  std::size_t max_id = std::numeric_limits<uint64_t>::min();
  {
	std::thread x(
	  [&]() {
		gg.for_each_sequence_line_in_file(
		  filename,
		  [&](gfak::sequence_elem s) {
			uint64_t id = stol(s.name);
			min_id = std::min(min_id, id);
			max_id = std::max(max_id, id);
		  });
	  });
	line_counts = gfa_line_counts(filename);
	x.join();
  }

  std::cout << "min_id: " << min_id << " max_id: " << max_id << std::endl;

  std::size_t node_count = line_counts['S'];
  std::size_t edge_count = line_counts['L'];
  std::size_t path_count = line_counts['P'];


  /*
	Build the digraph 
	-----------------
	  
  */
  
  // compute max nodes by difference between max and min ids
  std::size_t max_nodes = max_id - min_id + 1;

  digraph::DiGraph dg(max_nodes, path_count);

  // we want to start counting from 0 so we have to have an offset_value which would subtruct from the min_id
  // this is because the input graph is not guaranteed to start from 0
  // so we have to make sure that the graph starts from 0 as is the digraph
    
  std::size_t offset_value = min_id;
  std::cout << "offset_value: " << offset_value << std::endl;
	
  // add nodes
  // ---------
  {
	gg.for_each_sequence_line_in_file(
	  filename,
	  [&](const gfak::sequence_elem& s) {
		dg.create_handle(s.sequence, std::stoll(s.name) - offset_value);
	  });
  }

  assert(dg.size() == node_count);

  std::cout << "Nodes added "
			<< " Graph size: " << dg.size() << std::endl;
  
  // add edges
  // ---------
  {
	gg.for_each_edge_line_in_file(
	  filename,
	  [&](const gfak::edge_elem& e) {
		if (e.source_name.empty()) return;
		// TODO: is this not pointless computation for the sake of following the libhandlegraph the API?
		hg::handle_t a =
		  dg.get_handle(stoll(e.source_name) - offset_value,
						!e.source_orientation_forward);
		hg::handle_t b =
		  dg.get_handle(stoll(e.sink_name) - offset_value,
						!e.sink_orientation_forward);
		dg.create_edge(a, b);
	  });	
  }

  std::cout << "Edges added "
			<< " Graph size: " << dg.size() << std::endl;


  // TODO: use handles?
  //std::map<std::string, std::size_t> path_id_map;

  //std::vector<std::vector<std::pair<std::size_t, std::size_t>>> path_spans;
  
  std::size_t path_pos{0};

  // add paths
  // ---------
  // do this by associating each node with a path
  if (path_count > 0) {
	gg.for_each_path_line_in_file(
	  filename,
	  [&](const gfak::path_elem& path) {
		handlegraph::path_handle_t p_h = dg.create_path_handle(path.name);

		//metadata.path_id_map[path.name] = path_counter++;

		//auto x = std::vector<std::pair<std::size_t, std::size_t>>();
		//x.reserve(path.segment_names.size());

		//metadata.path_spans.push_back(x);

		//std::vector<std::pair<std::size_t, std::size_t>>& curr_spans =
		//metadata.path_spans.back();

		path_pos = 0;
		
		for (auto& s : path.segment_names) {
		  
		  handlegraph::nid_t id = std::stoull(s) - offset_value;

		  // TODO: go through the handle graph API		  
		  // this can be done through handle but this is preferable in my case
		  digraph::Vertex& v = dg.get_vertex_mut(id);

		  //if (curr_spans.empty()) {
		  //curr_spans.push_back(std::make_pair(0, v.get_seq().length()));
		  //}
		  //else {
		  //	std::pair<std::size_t, std::size_t> prev =
		  //  curr_spans.back();
		  //std::pair<std::size_t, std::size_t> curr =
		  //  std::make_pair(prev.second + prev.first, v.get_seq().length());
			
		  //curr_spans.push_back(curr);
		  //}

		  if (v.add_path(std::stoll(p_h.data), path_pos) < 0) {
			std::cout << "error setting path" << std::endl;
		  }

		  path_pos += v.get_seq().length();
		  //std::cout << "path_pos: " << path_pos << std::endl;
		}

	  });
  }

  std::cout << "Paths added "
			<< " Graph size: " << dg.size() << std::endl;

  dg.compute_start_nodes();
  dg.compute_stop_nodes();

  return dg;
}
  
} // namespace io
