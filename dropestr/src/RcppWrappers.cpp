#include "dropestr.h"

using namespace Rcpp;

s_vec_t GetUmisList(unsigned umi_len);

slst_map_t parseList(const List &lst) {
  slst_map_t res;
  StringVector names = as<StringVector>(lst.names());
  for (int i = 0; i < lst.size(); ++i) {
    res.emplace(std::string(names[i]), lst[i]);
  }

  return res;
}

si_map_t parseVector(const IntegerVector &vec) {
  si_map_t res;
  StringVector names = as<StringVector>(vec.names());
  for (int i = 0; i < vec.size(); ++i) {
    res.emplace(std::string(names[i]), vec[i]);
  }

  return res;
}

sd_map_t parseVector(const NumericVector &vec) {
  sd_map_t res;
  StringVector names = as<StringVector>(vec.names());
  for (int i = 0; i < vec.size(); ++i) {
    res.emplace(std::string(names[i]), vec[i]);
  }

  return res;
}

NumericVector vpow(double base, const NumericVector& exp) {
  NumericVector res(exp.size());
  for (int i = 0; i < res.size(); ++i) {
    res[i] = std::pow(base, exp[i]);
  }
  return res;
}

NumericVector vpow(const NumericVector& base, double exp) {
  NumericVector res(base.size());
  for (int i = 0; i < res.size(); ++i) {
    res[i] = std::pow(base[i], exp);
  }
  return res;
}

// [[Rcpp::export]]
si_map_t ValueCountsC(const s_vec_t &values) {
  si_map_t res;
  for (auto const &value : values) {
    res[value]++;
  }

  return res;
}

//' @export
// [[Rcpp::export]]
std::unordered_map<int, int> ValueCounts(const std::vector<int> &values) {
  std::unordered_map<int, int> res;
  for (int value : values) {
    res[value]++;
  }

  return res;
}