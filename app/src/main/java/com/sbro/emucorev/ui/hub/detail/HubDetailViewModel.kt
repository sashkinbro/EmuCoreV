package com.sbro.emucorev.ui.hub.detail

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.data.hub.HubArticle
import com.sbro.emucorev.data.hub.HubItem
import com.sbro.emucorev.data.hub.HubRepository
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class HubDetailUiState(
    val article: HubArticle? = null,
    val relatedItems: List<HubItem> = emptyList(),
    val isLoading: Boolean = true,
    val isUpdatingFavorite: Boolean = false,
    val error: Throwable? = null
)

class HubDetailViewModel(
    application: Application,
    private val contentId: String
) : AndroidViewModel(application) {
    private val repository = HubRepository.get(application)
    private val _uiState = MutableStateFlow(HubDetailUiState())
    val uiState: StateFlow<HubDetailUiState> = _uiState.asStateFlow()

    init {
        load()
    }

    fun retry() = load()

    fun toggleFavorite() {
        val article = _uiState.value.article ?: return
        if (_uiState.value.isUpdatingFavorite) return
        viewModelScope.launch {
            _uiState.update { it.copy(isUpdatingFavorite = true) }
            val favorite = repository.toggleFavorite(article.item)
            _uiState.update {
                it.copy(
                    article = article.copy(item = article.item.copy(isFavorite = favorite)),
                    isUpdatingFavorite = false
                )
            }
        }
    }

    private fun load() {
        viewModelScope.launch {
            _uiState.value = HubDetailUiState(isLoading = true)
            runCatching {
                val article = repository.loadArticle(contentId)
                article to runCatching { repository.relatedItems(contentId) }.getOrDefault(emptyList())
            }
                .onSuccess { (article, relatedItems) ->
                    _uiState.value = HubDetailUiState(
                        article = article,
                        relatedItems = relatedItems,
                        isLoading = false
                    )
                }
                .onFailure { error -> _uiState.value = HubDetailUiState(isLoading = false, error = error) }
        }
    }

    class Factory(
        private val application: Application,
        private val contentId: String
    ) : ViewModelProvider.Factory {
        @Suppress("UNCHECKED_CAST")
        override fun <T : ViewModel> create(modelClass: Class<T>): T {
            return HubDetailViewModel(application, contentId) as T
        }
    }
}
