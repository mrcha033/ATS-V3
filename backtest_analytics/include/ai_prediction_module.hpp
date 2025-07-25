#pragma once

#include "data_loader.hpp"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <chrono>

namespace ats {
namespace backtest {

// Feature vector for ML model input
struct FeatureVector {
    std::vector<double> price_features;      // OHLC, moving averages, etc.
    std::vector<double> volume_features;     // Volume indicators
    std::vector<double> spread_features;     // Spread-related features
    std::vector<double> volatility_features; // Volatility indicators
    std::vector<double> technical_features;  // Technical indicators
    
    std::chrono::system_clock::time_point timestamp;
    std::string symbol;
    std::string exchange;
    
    // Convert to flat vector for ML model
    std::vector<double> to_flat_vector() const;
    size_t feature_count() const;
};

// Prediction result from ML model
struct PredictionResult {
    double spread_prediction = 0.0;        // Predicted spread in next period
    double confidence_score = 0.0;         // Model confidence (0.0 to 1.0)
    double price_direction = 0.0;          // Predicted price direction (-1.0 to 1.0)
    double volatility_prediction = 0.0;    // Predicted volatility
    
    std::chrono::system_clock::time_point prediction_time;
    std::chrono::system_clock::time_point target_time;
    std::string symbol;
    std::string model_version;
    
    // Risk assessment
    double risk_score = 0.0;               // Risk level (0.0 to 1.0)
    std::string risk_category;             // "low", "medium", "high"
    
    // Feature importance (for explainability)
    std::unordered_map<std::string, double> feature_importance;
};

// Configuration for AI prediction
struct AIPredictionConfig {
    std::string model_type = "linear_regression";  // "linear_regression", "random_forest", "neural_network"
    std::string model_file_path;                   // Path to saved model
    
    // Feature configuration
    int price_window_size = 20;                    // Historical price window
    int volume_window_size = 10;                   // Volume analysis window
    bool use_technical_indicators = true;          // Include technical indicators
    bool use_spread_features = true;               // Include spread analysis
    
    // Prediction parameters
    int prediction_horizon_minutes = 5;            // How far ahead to predict
    double confidence_threshold = 0.6;             // Minimum confidence for predictions
    double update_frequency_seconds = 60.0;        // How often to update predictions
    
    // Model training parameters
    double train_test_split = 0.8;                 // Training/testing data split
    int max_training_samples = 10000;              // Maximum samples for training
    bool enable_online_learning = false;           // Enable continuous learning
    
    // Performance thresholds
    double min_accuracy = 0.55;                    // Minimum model accuracy
    double max_prediction_age_minutes = 10.0;      // Maximum age of predictions to use
};

// Feature engineering class
class FeatureEngineer {
public:
    FeatureEngineer();
    ~FeatureEngineer();
    
    // Extract features from market data
    FeatureVector extract_features(const std::vector<MarketDataPoint>& historical_data,
                                  const MarketDataPoint& current_data) const;
    
    // Technical indicator calculations
    std::vector<double> calculate_moving_averages(const std::vector<double>& prices,
                                                 const std::vector<int>& periods) const;
    std::vector<double> calculate_rsi(const std::vector<double>& prices, int period = 14) const;
    std::vector<double> calculate_bollinger_bands(const std::vector<double>& prices,
                                                 int period = 20, double std_dev = 2.0) const;
    std::vector<double> calculate_macd(const std::vector<double>& prices,
                                      int fast_period = 12, int slow_period = 26, int signal_period = 9) const;
    
    // Volume indicators
    std::vector<double> calculate_volume_indicators(const std::vector<MarketDataPoint>& data) const;
    double calculate_volume_weighted_price(const std::vector<MarketDataPoint>& data) const;
    
    // Volatility indicators
    std::vector<double> calculate_volatility_features(const std::vector<double>& prices) const;
    double calculate_atr(const std::vector<MarketDataPoint>& data, int period = 14) const;
    
    // Spread-specific features
    std::vector<double> calculate_spread_features(
        const std::vector<MarketDataPoint>& exchange1_data,
        const std::vector<MarketDataPoint>& exchange2_data) const;
    
    // Feature normalization
    std::vector<double> normalize_features(const std::vector<double>& features) const;
    
private:
    mutable std::unordered_map<std::string, std::pair<double, double>> normalization_params_; // mean, std
    
    // Helper functions
    double calculate_standard_deviation(const std::vector<double>& values) const;
    double calculate_correlation(const std::vector<double>& x, const std::vector<double>& y) const;
    std::vector<double> apply_exponential_smoothing(const std::vector<double>& data, double alpha = 0.3) const;
};

// Base class for ML models
class MLModel {
public:
    virtual ~MLModel() = default;
    
    // Model interface
    virtual bool train(const std::vector<FeatureVector>& training_data,
                      const std::vector<double>& target_values) = 0;
    virtual PredictionResult predict(const FeatureVector& features) = 0;
    virtual double evaluate(const std::vector<FeatureVector>& test_data,
                           const std::vector<double>& test_targets) = 0;
    
    // Model persistence
    virtual bool save_model(const std::string& file_path) = 0;
    virtual bool load_model(const std::string& file_path) = 0;
    
    // Model info
    virtual std::string get_model_type() const = 0;
    virtual std::string get_model_version() const = 0;
    virtual size_t get_feature_count() const = 0;
    
protected:
    bool is_trained_ = false;
    std::string model_version_ = "1.0";
    size_t feature_count_ = 0;
};

// Simple linear regression model
class LinearRegressionModel : public MLModel {
public:
    LinearRegressionModel();
    ~LinearRegressionModel() override = default;
    
    bool train(const std::vector<FeatureVector>& training_data,
              const std::vector<double>& target_values) override;
    PredictionResult predict(const FeatureVector& features) override;
    double evaluate(const std::vector<FeatureVector>& test_data,
                   const std::vector<double>& test_targets) override;
    
    bool save_model(const std::string& file_path) override;
    bool load_model(const std::string& file_path) override;
    
    std::string get_model_type() const override { return "LinearRegression"; }
    std::string get_model_version() const override { return model_version_; }
    size_t get_feature_count() const override { return feature_count_; }
    
private:
    std::vector<double> weights_;
    double bias_ = 0.0;
    double training_mse_ = 0.0;
    
    // Helper functions
    std::vector<double> matrix_vector_multiply(const std::vector<std::vector<double>>& matrix,
                                              const std::vector<double>& vector) const;
    std::vector<std::vector<double>> matrix_transpose(const std::vector<std::vector<double>>& matrix) const;
    std::vector<std::vector<double>> matrix_multiply(const std::vector<std::vector<double>>& a,
                                                    const std::vector<std::vector<double>>& b) const;
    std::vector<std::vector<double>> matrix_inverse(const std::vector<std::vector<double>>& matrix) const;
};

// Random Forest model (simplified implementation)
class RandomForestModel : public MLModel {
public:
    RandomForestModel(int n_trees = 10, int max_depth = 5);
    ~RandomForestModel() override = default;
    
    bool train(const std::vector<FeatureVector>& training_data,
              const std::vector<double>& target_values) override;
    PredictionResult predict(const FeatureVector& features) override;
    double evaluate(const std::vector<FeatureVector>& test_data,
                   const std::vector<double>& test_targets) override;
    
    bool save_model(const std::string& file_path) override;
    bool load_model(const std::string& file_path) override;
    
    std::string get_model_type() const override { return "RandomForest"; }
    std::string get_model_version() const override { return model_version_; }
    size_t get_feature_count() const override { return feature_count_; }
    
private:
    struct DecisionNode {
        int feature_index = -1;
        double threshold = 0.0;
        double prediction = 0.0;
        std::unique_ptr<DecisionNode> left;
        std::unique_ptr<DecisionNode> right;
        bool is_leaf = false;
    };
    
    struct DecisionTree {
        std::unique_ptr<DecisionNode> root;
        double predict(const std::vector<double>& features) const;
    };
    
    int n_trees_;
    int max_depth_;
    std::vector<DecisionTree> trees_;
    std::vector<std::vector<int>> feature_subsets_;
    
    // Tree building functions
    std::unique_ptr<DecisionNode> build_tree(const std::vector<std::vector<double>>& features,
                                            const std::vector<double>& targets,
                                            const std::vector<int>& feature_subset,
                                            int depth = 0);
    std::pair<int, double> find_best_split(const std::vector<std::vector<double>>& features,
                                          const std::vector<double>& targets,
                                          const std::vector<int>& feature_subset);
    double calculate_variance(const std::vector<double>& values) const;
};

// Main AI Prediction Module
class AIPredictionModule {
public:
    AIPredictionModule();
    ~AIPredictionModule();
    
    // Configuration
    bool initialize(const AIPredictionConfig& config);
    AIPredictionConfig get_config() const;
    void set_config(const AIPredictionConfig& config);
    
    // Model management
    bool train_model(const std::vector<MarketDataPoint>& historical_data);
    bool load_pretrained_model(const std::string& model_path);
    bool save_current_model(const std::string& model_path);
    
    // Prediction interface
    PredictionResult predict_spread(const std::vector<MarketDataPoint>& recent_data,
                                   const std::string& symbol,
                                   const std::string& exchange1,
                                   const std::string& exchange2);
    
    std::vector<PredictionResult> batch_predict(
        const std::vector<std::vector<MarketDataPoint>>& batch_data);
    
    // Model evaluation and validation
    double validate_model(const std::vector<MarketDataPoint>& validation_data);
    std::unordered_map<std::string, double> get_model_metrics();
    
    // Feature analysis
    std::vector<std::string> get_feature_names() const;
    std::unordered_map<std::string, double> get_feature_importance();
    
    // Online learning (if enabled)
    bool update_model_online(const std::vector<MarketDataPoint>& new_data,
                            const std::vector<double>& actual_results);
    
    // Utility functions
    bool is_model_ready() const;
    std::chrono::system_clock::time_point get_last_training_time() const;
    size_t get_training_sample_count() const;
    
    // Performance monitoring
    void log_prediction_accuracy(const PredictionResult& prediction, double actual_value);
    double get_recent_accuracy(int days = 7) const;
    
private:
    AIPredictionConfig config_;
    std::unique_ptr<MLModel> model_;
    std::unique_ptr<FeatureEngineer> feature_engineer_;
    
    // Training data management
    std::vector<FeatureVector> training_features_;
    std::vector<double> training_targets_;
    std::chrono::system_clock::time_point last_training_time_;
    
    // Performance tracking
    struct PredictionRecord {
        PredictionResult prediction;
        double actual_value;
        std::chrono::system_clock::time_point recorded_time;
    };
    std::vector<PredictionRecord> prediction_history_;
    
    // Cache for recent predictions
    std::unordered_map<std::string, PredictionResult> prediction_cache_;
    std::chrono::system_clock::time_point last_prediction_update_;
    
    // Helper functions
    std::unique_ptr<MLModel> create_model(const std::string& model_type);
    std::vector<double> prepare_target_values(const std::vector<MarketDataPoint>& data);
    bool is_prediction_valid(const PredictionResult& prediction) const;
    std::string generate_cache_key(const std::string& symbol, 
                                  const std::string& exchange1,
                                  const std::string& exchange2) const;
    
    // Data preprocessing
    std::vector<MarketDataPoint> filter_data_by_timerange(
        const std::vector<MarketDataPoint>& data,
        std::chrono::system_clock::time_point start_time,
        std::chrono::system_clock::time_point end_time) const;
    
    std::vector<MarketDataPoint> resample_data_for_training(
        const std::vector<MarketDataPoint>& data) const;
    
    // Model validation
    bool validate_model_performance();
    void cleanup_old_predictions();
};

// Exception classes
class AIPredictionException : public std::exception {
public:
    explicit AIPredictionException(const std::string& message) : message_(message) {}
    const char* what() const noexcept override { return message_.c_str(); }
    
private:
    std::string message_;
};

class ModelTrainingException : public AIPredictionException {
public:
    explicit ModelTrainingException(const std::string& message) 
        : AIPredictionException("Model Training Error: " + message) {}
};

class FeatureExtractionException : public AIPredictionException {
public:
    explicit FeatureExtractionException(const std::string& message) 
        : AIPredictionException("Feature Extraction Error: " + message) {}
};

class ModelLoadException : public AIPredictionException {
public:
    explicit ModelLoadException(const std::string& message) 
        : AIPredictionException("Model Load Error: " + message) {}
};

} // namespace backtest
} // namespace ats