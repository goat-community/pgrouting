/*PGR-GNU*****************************************************************
File: pgr_kruskal.hpp

Copyright (c) 2015 pgRouting developers
Mail: project@pgrouting.org

Copyright (c) 2018 Aditya Pratap Singh
adityapratap.singh28@gmail.com

------
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
********************************************************************PGR-GNU*/

#ifndef INCLUDE_MST_PGR_KRUSKAL_HPP_
#define INCLUDE_MST_PGR_KRUSKAL_HPP_
#pragma once

#include <boost/config.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/connected_components.hpp>
#include <boost/graph/depth_first_search.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/kruskal_min_spanning_tree.hpp>
#include <boost/graph/filtered_graph.hpp>

#include <iostream>
#include <numeric>
#include <vector>
#include <algorithm>
#include <sstream>
#include <functional>
#include <limits>

#include "cpp_common/basePath_SSEC.hpp"
#include "cpp_common/pgr_base_graph.hpp"

namespace pgrouting {

namespace  visitors {

template <class E>
class Bfs_visitor : public boost::default_bfs_visitor {
    public:
        explicit Bfs_visitor(
                std::vector<E> &data) :
            m_data(data)  {}
        template <class B_G>
            void tree_edge(E e, const B_G&) {
                m_data.push_back(e);
            }
    private:
        std::vector<E> &m_data;
};

template <class E>
class Dfs_visitor : public boost::default_dfs_visitor {
    public:
        explicit Dfs_visitor(
                std::vector<E> &data) :
            m_data(data)  {}
        template <typename B_G>
            void tree_edge(E e, const B_G&) {
                m_data.push_back(e);
            }
    private:
        std::vector<E> &m_data;
};

}  // namespace visitors



namespace functions {

template <class G>
class Pgr_kruskal {
 public:
     std::vector<pgr_kruskal_t> operator() (
             G &graph,
             int order_by,
             bool get_component);

 private:
     typedef typename G::B_G B_G;
     typedef typename G::V V;
     typedef typename G::E E;

     std::vector<pgr_kruskal_t> generateKruskal(
             G &graph,
             int order_by,
             bool get_component);

     std::vector<int64_t> component_id(
             bool get_component,
             const G &graph);

     std::vector<pgr_kruskal_t> order_results(
             int order_by,
             bool get_component,
             const G &graph);

     template <typename T>
     std::vector<pgr_kruskal_t> get_results(
             T order,
             bool get_component,
             const G &graph) {
         for (const auto edge : order) {
             m_results.push_back({
                 get_component? m_tree_id[m_components[graph.source(edge)]] : 0,
                 graph[graph.source(edge)].id,
                 graph[graph.target(edge)].id,
                 graph[edge].id,
                 graph[edge].cost,
                 0
             });
         }
         return m_results;
     }

     std::vector<pgr_kruskal_t> get_agg_cost(
             bool get_component,
             const G &graph) {
         std::vector<pgr_kruskal_t> results;
         std::vector<double> agg_cost(graph.num_vertices());
         for (const auto edge : m_results) {
             if (agg_cost[graph.get_V(edge.nodes)] == 0) {
                 results.push_back({
                     edge.component, -1, edge.nodes, -1, 0.0, 0.0 });
             }
             agg_cost[graph.get_V(edge.nodet)] = agg_cost[graph.get_V(edge.nodes)] + edge.cost;

             results.push_back({
                 edge.component,
                 -1,
                 edge.nodet,
                 edge.edge,
                 edge.cost,
                 agg_cost[graph.get_V(edge.nodet)],
             });
         }
         return results;
     }

 private:
     struct InSpanning {
         std::set<E> edges;
         bool operator()(E e) const { return edges.count(e); }
         void clear() { edges.clear(); }
     } spanning_tree;

     /** m_components[v]:
      *  - is empty (when get_component = 0)
      *  - has the component number of vertex v (when get_component != 0)
      */
     std::vector<size_t> m_components;
     /** m_tree_id[v]:
      *  - is empty (when get_component = 0)
      *  - has the component number of vertex v (when get_component = 1)
      *  - has the min vertex id that belongs to the component (when get_component = 2)
      */
     std::vector<int64_t> m_tree_id;
     std::vector<pgr_kruskal_t> m_results;

};



/* IMPLEMENTATION */

/* @brief maps component number with smallest original vertex id
 *
 *
 */
template <class G>
std::vector<int64_t>
Pgr_kruskal<G>::component_id(bool get_component, const G &graph) {
    if (!get_component) {
        return std::vector<int64_t>();
    }

    m_components.resize(num_vertices(graph.graph));

    /*
     * Calculate connected components
     *
     * Number of components of graph: num_comps components
     */
    auto num_comps = boost::connected_components(
            graph.graph,
            &m_components[0]);

    std::vector<int64_t> tree_id(num_comps, 0);

    for (const auto v : boost::make_iterator_range(vertices(graph.graph))) {
        tree_id[m_components[v]] =
            (tree_id[m_components[v]] == 0
             || tree_id[m_components[v]] >= graph[v].id) ?
            graph[v].id : tree_id[m_components[v]];
    }
    return tree_id;
}



template <class G>
std::vector<pgr_kruskal_t>
Pgr_kruskal<G>::order_results(int order_by, bool get_component, const G &graph) {
    /*
     * No particular order
     */
    if (order_by == 0) {
        return get_results(spanning_tree.edges, get_component, graph);
    }



    typedef typename G::B_G B_G;
    typedef typename G::E E;

    boost::filtered_graph<B_G, InSpanning, boost::keep_all> mst(graph.graph, spanning_tree, {});

    /*
     * order b dfs
     */
    if (order_by == 1 ) {
        std::vector<E> visited_order;

        using dfs_visitor = visitors::Dfs_visitor<E>;
        boost::depth_first_search(mst, visitor(dfs_visitor(visited_order)));

        get_results(visited_order, get_component, graph);
        return get_agg_cost(get_component, graph);
    }

    /*
     * order biy bfs
     */
    using bfs_visitor = visitors::Bfs_visitor<E>;
    for (auto root : m_tree_id) {
        std::vector<E> visited_order;
        boost::breadth_first_search(mst,
                graph.get_V(root),
                visitor(bfs_visitor(visited_order)));

        get_results(visited_order, get_component, graph);
    }

    return get_agg_cost(get_component, graph);
}



template <class G>
std::vector<pgr_kruskal_t>
Pgr_kruskal<G>::generateKruskal(
        G &graph,
        int order_by,
        bool get_component) {
    spanning_tree.clear();
    m_components.clear();
    m_results.clear();
    m_tree_id.clear();

    boost::kruskal_minimum_spanning_tree(
            graph.graph,
            std::inserter(spanning_tree.edges, spanning_tree.edges.end()),
            boost::weight_map(get(&G::G_T_E::cost, graph.graph)));

    m_tree_id = component_id(get_component, graph);

    return order_results(order_by, get_component, graph);
}

template <class G>
std::vector<pgr_kruskal_t>
Pgr_kruskal<G>::operator() (
        G &graph,
        int order_by,
        bool get_component) {
    get_component = get_component || (order_by == 2);
    return generateKruskal(graph, order_by, get_component);
}


}  // namespace functions
}  // namespace pgrouting

#endif  // INCLUDE_MST_PGR_KRUSKAL_HPP_
