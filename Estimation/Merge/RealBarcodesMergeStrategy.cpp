#include <boost/range/adaptor/reversed.hpp>
#include <Tools/Logs.h>
#include <fstream>
#include <Tools/UtilFunctions.h>
#include "RealBarcodesMergeStrategy.h"

namespace Estimation
{
namespace Merge
{
	using Tools::IndexedValue;

	RealBarcodesMergeStrategy::RealBarcodesMergeStrategy(const std::string &barcodes_filename, size_t barcode2_length,
														 int min_genes_before_merge, int min_genes_after_merge,
														 int max_merge_edit_distance, double min_merge_fraction)
			: AbstractMergeStrategy(min_genes_before_merge, min_genes_after_merge, max_merge_edit_distance, min_merge_fraction)
			, _barcodes_filename(barcodes_filename)
			, _barcode2_length(barcode2_length)
	{}

	void Merge::RealBarcodesMergeStrategy::merge(Estimation::CellsDataContainer &container,
												 const s_uu_hash_t &umig_cells_counts, ids_t &filtered_cells) const
	{
		names_t cbs1, cbs2;
		std::vector<bool> is_cell_real(container.cell_barcodes().size(), false);

		RealBarcodesMergeStrategy::get_barcodes_list(this->_barcodes_filename, cbs1, cbs2);
		if (cbs1.size() == 0)
			return;

		ISIHM cb_reassigned_to_it;
		ids_t cb_reassign_targets(container.cell_barcodes().size());
		std::iota(cb_reassign_targets.begin(), cb_reassign_targets.end(), 0);

		size_t tag_index = 0, merges_count = 0;

		for (const IndexedValue &genes_count : boost::adaptors::reverse(container.cells_genes_counts_sorted()))
		{
			if (++tag_index % 1000 == 0)
			{
				L_TRACE << "Total " << tag_index << " tags processed, " << merges_count << " cells merged";
			}

			long real_cell_ind = this->get_real_cb(container, genes_count.index, cbs1, cbs2);

			if (real_cell_ind == genes_count.index)
			{
				is_cell_real[genes_count.index] = true;
				continue;
			}

			if (real_cell_ind < 0)
			{
				container.exclude_cell(genes_count.index);
				continue;
			}

			this->merge_force(container, genes_count.index, (size_t)real_cell_ind, cb_reassign_targets, cb_reassigned_to_it);
			merges_count++;
		}
		L_INFO << "Total " << merges_count << " merges";

		container.update_cells_genes_counts(this->min_genes_after_merge(), false);
		for (const IndexedValue &gene_count : boost::adaptors::reverse(container.cells_genes_counts_sorted()))
		{
			if (!is_cell_real[gene_count.index])
				continue;

			L_DEBUG << "Add cell to filtered: " << gene_count.value << " " << gene_count.index;
			filtered_cells.push_back(gene_count.index);
		}

		container.stats().merge(cb_reassign_targets, container.cell_barcodes());
	}

	void RealBarcodesMergeStrategy::get_barcodes_list(const std::string &barcodes_filename, names_t &barcodes1,
													  names_t &barcodes2)
	{
		std::ifstream cb_f(barcodes_filename);
		if (cb_f.fail())
			throw std::runtime_error("Can't open barcodes file: '" + barcodes_filename + "'");

		std::string line;
		while (std::getline(cb_f, line))
		{
			size_t space_ind = line.find(' ');
			if (space_ind == std::string::npos)
			{
				L_WARN << "WARNING: barcodes line has bad format: '" << line << "'";
				continue;
			}

			barcodes1.push_back(Tools::reverse_complement(line.substr(0, space_ind)));
			barcodes2.push_back(Tools::reverse_complement(line.substr(space_ind + 1)));
		}

		if (barcodes1.size() == 0)
		{
			L_WARN << "WARNING: empty barcodes list";
		}
	}

	long RealBarcodesMergeStrategy::get_real_cb(const Estimation::CellsDataContainer &container, size_t base_cell_ind,
												const names_t &cbs1, const names_t &cbs2) const
	{
		std::string base_cb = container.cell_barcode(base_cell_ind);
		i_counter_t dists1, dists2;

		std::string cb_part1 = base_cb.substr(0, base_cb.length() - this->_barcode2_length);
		std::string cb_part2 = base_cb.substr(base_cb.length() - this->_barcode2_length + 1);

		RealBarcodesMergeStrategy::fill_distances_to_cb(cb_part1, cb_part2, cbs1, cbs2, dists1, dists2);

		if (dists1[0].value == 0 && dists2[0].value == 0)
			return base_cell_ind;

		L_DEBUG <<"Get real neighbours to " << base_cb;
		ids_t neighbour_cells = this->get_real_neighbour_cbs(container, cbs1, cbs2, base_cb, dists1, dists2);
		if (neighbour_cells.empty())
			return -1;

		return this->get_best_merge_target(container, base_cell_ind, neighbour_cells);

	}

	long RealBarcodesMergeStrategy::get_best_merge_target(const CellsDataContainer &container, size_t base_cell_ind,
														  const IMergeStrategy::ids_t &neighbour_cells) const
	{
		double max_umigs_intersection_frac = 0;
		size_t best_neighbour_cell_ind = neighbour_cells[0];
		for (size_t neighbour_cell_ind: neighbour_cells)
		{
			double current_frac = this->get_umigs_intersect_fraction(container, base_cell_ind, neighbour_cell_ind);
			if (max_umigs_intersection_frac < current_frac)
			{
				max_umigs_intersection_frac = current_frac;
				best_neighbour_cell_ind = neighbour_cell_ind;
			}
//			this->_stats.add_str(Stats::MERGE_INTERSECT_SIZE_BY_CELL, this->_cell_barcodes[neighbour_cell_ind],
//			                     base_cb, umigs_intersection_size);
		}
//		this->_stats.add_str(Stats::MERGE_REAL_INTERSECT_SIZE_BY_CELL, this->_cell_barcodes[best_neighbour_cell_ind],
//		                     base_cb, max_umigs_intersection_frac);

		if (max_umigs_intersection_frac < this->_min_merge_fraction)
			return base_cell_ind;

		return best_neighbour_cell_ind;
	}

	RealBarcodesMergeStrategy::ids_t RealBarcodesMergeStrategy::get_real_neighbour_cbs(const Estimation::CellsDataContainer &container,
																					   const names_t &cbs1, const names_t &cbs2,
																					   const std::string &base_cb,
																					   const i_counter_t &dists1,
																					   const i_counter_t &dists2) const
	{
		typedef std::tuple<size_t, size_t, long> tuple_t;
		std::vector<tuple_t> real_cell_inds;
		for (size_t ind1 = 0; ind1 < dists1.size(); ++ind1)
		{
			auto const &cur_dist1 = dists1[ind1];
			if (cur_dist1.value + dists2[0].value > RealBarcodesMergeStrategy::MAX_REAL_MERGE_EDIT_DISTANCE)
				break;

			for (size_t ind2 = 0; ind2 < dists2.size(); ++ind2)
			{
				auto const &cur_dist2 = dists2[ind2];
				long cur_ed = cur_dist1.value + cur_dist2.value;
				if (cur_ed > RealBarcodesMergeStrategy::MAX_REAL_MERGE_EDIT_DISTANCE)
					break;

				real_cell_inds.push_back(std::make_tuple(cur_dist1.index, cur_dist2.index, cur_ed));
			}
		}

		sort(real_cell_inds.begin(), real_cell_inds.end(), [](const tuple_t &t1, const tuple_t &t2){ return std::get<2>(t1) < std::get<2>(t2);});

		ids_t neighbour_cbs;
		long prev_dist = std::numeric_limits<long>::max();
		for (auto const & inds: real_cell_inds)
		{
			long cur_ed = std::get<2>(inds);
			if (cur_ed > prev_dist && !neighbour_cbs.empty())
				break;

			std::string current_cb = cbs1[std::get<0>(inds)] + cbs2[std::get<1>(inds)];
			auto const current_cell_it = container.cell_ids_by_cb().find(current_cb);
			if (current_cell_it != container.cell_ids_by_cb().end())
			{
				neighbour_cbs.push_back(current_cell_it->second);
				container.stats().add_str(Estimation::Stats::MERGE_EDIT_DISTANCE_BY_CELL, current_cb, base_cb, cur_ed);
			}
			else
			{
				container.stats().add_str(Estimation::Stats::MERGE_REJECTION_BY_CELL, current_cb, base_cb, cur_ed);
			}
			prev_dist = cur_ed;
		}

		return neighbour_cbs;
	}

	double RealBarcodesMergeStrategy::get_umigs_intersect_fraction(const Estimation::CellsDataContainer &container,
																   size_t cell1_ind, size_t cell2_ind) const
	{
		const auto &cell1 = container.cell_genes(cell1_ind);
		const auto &cell2 = container.cell_genes(cell2_ind);

		auto gene1_it = cell1.begin();
		auto gene2_it = cell2.begin();

		size_t intersect_size = 0, cell1_umigs = 0, cell2_umigs= 0;
		while (gene1_it != cell1.end() && gene2_it != cell2.end())
		{
			int comp_res = gene1_it->first.compare(gene2_it->first);
			if (comp_res < 0)
			{
				cell1_umigs += gene1_it->second.size();
				gene1_it++;
				continue;
			}

			if (comp_res > 0)
			{
				cell2_umigs += gene2_it->second.size();
				gene2_it++;
				continue;
			}

			auto umi1_it = gene1_it->second.begin();
			auto umi2_it = gene2_it->second.begin();

			while (umi1_it != gene1_it->second.end() && umi2_it != gene2_it->second.end())
			{
				comp_res = umi1_it->first.compare(umi2_it->first);
				if (comp_res < 0)
				{
					++umi1_it;
					continue;
				}

				if (comp_res > 0)
				{
					++umi2_it;
					continue;
				}

				++intersect_size;
				++umi2_it;
				++umi1_it;
			}

			cell1_umigs += gene1_it->second.size();
			cell2_umigs += gene2_it->second.size();
			++gene1_it;
			++gene2_it;
		}

		return intersect_size / (double) std::min(cell1_umigs, cell2_umigs);
	}

	void RealBarcodesMergeStrategy::fill_distances_to_cb(const std::string &cb_part1, const std::string &cb_part2,
														 const names_t &cbs1, const names_t &cbs2,
														 i_counter_t &dists1, i_counter_t &dists2)
	{
		dists1.reserve(cbs1.size());
		dists2.reserve(cbs2.size());

		size_t i = 0;
		for (auto const cb1: cbs1)
		{
			dists1.push_back(IndexedValue(i, Tools::edit_distance(cb_part1.c_str(), cb1.c_str())));
			i++;
		}

		i = 0;
		for (auto const cb2: cbs2)
		{
			dists2.push_back(IndexedValue(i, Tools::edit_distance(cb_part2.c_str(), cb2.c_str())));
			i++;
		}

		sort(dists1.begin(), dists1.end(), IndexedValue::value_less);
		sort(dists2.begin(), dists2.end(), IndexedValue::value_less);
	}
}
}