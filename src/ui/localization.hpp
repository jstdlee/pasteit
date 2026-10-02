#pragma once
#include "config/app_settings.hpp"
#include <string>
#include <string_view>
namespace pasteit {
enum class UiTextKey{
    SmartActions,RecentPaths,Settings,General,Copy,Open,Move,Save,Cancel,PromptTemplates,GeneralLlm,Djev,
    Target,NoClipboard,WaitingDjev,NoRankedActions,Type,Name,Path,Source,Actions,Folder,File,CopyHere,MoveHere,
    CopyPath,OpenResult,OpenFolder,ImagePreview,CopyImage,CopyTemporaryPath,OpenTemporaryFile,AiResult,Generating,CopyResult,
    ReplaceClipboard,Retry,Language,Opacity,DefaultImageDirectory,DefaultTextDirectory,DjevEndpoint,DjevModel,
    DjevApiKey,LlmEndpoint,LlmModel,LlmApiKey,TestDjev,TestGeneralLlm,TemplateName,SystemPrompt,Enabled,
    Temperature,Duplicate,Delete,ConfirmDelete,Keep,NewTemplate,RestoreDefaults,ResetGeneral,ResetPaths,
    ResetProviders,DeleteQuestion,KeyboardHint,Zoom,DjevTestSucceeded,DjevTestFailed,
    GeneralLlmTestSucceeded,GeneralLlmTestFailed,Saved,OpenedTemporaryImage,KeepLatest10,DeleteAllHistory,
    RefreshModels,ModelList,ModelListFailed,HistoryPruned,HistoryDeleted,
    ClipboardHistory,Preview,Size,Captured,Details,View,Edit,Close,MimeTypes,MissingData,PromptActions,
    ConfigureGeneralLlm,BuiltIn,Custom,Browse,Destination,Filename,OutputPath,Confirm,InvalidPath,
    InvalidFilename,SourceParent,FocusedDirectory,ManualDestination,DeleteTemplateText,DuplicateTemplateText,
    UseAsDestination,OpenParent,Use,Seen,Parent,Reference,CopyHereAction,MoveHereAction,ConfirmFileOperation,
    GraphData,GraphPreview,ChartType,HeaderRow,ConvertDates,SavePngAs,CopyGraphImage,SaveGraphImage,
    CustomPrompt,PromptInstructions,Generate,AnnotateImage,
    UsageInsights,UsageInsightsHelp,UsageInsightsEmpty,ResetUsage,UsageReset,FastActions,
    StatusIdle,StatusRanking,StatusRanked,StatusFallback,ClickToCopy,Theme,ThemeDark,ThemeLight,
    ConfirmOverwrite,FastAnnotation,Comment,Undo,Clear,SaveAnnotatedSvg,CopyTemporaryImagePath,OpenPngExternally,Pause,Resume,ClipboardHistoryDetail,RecentPathDetail,MermaidRendererHelp,MermaidHomepage,MermaidCliPath,MermaidCliArguments,QrHelp,QrHomepage,QrErrorCorrection,QrMargin,QrScale,DownloadResumeDirectory,KeepPartFiles,TerminalCommand,TerminalProfile,ShowSha256,ShowSha512,DateSpacingEnabled,Points,Uses,ViewTable,PreviewMarkdown,Pipeline,Anonymize,
    PromptThinking,PromptThinkingHelp,ThinkingColumn,ModelThinking,
    Privacy,DjevHelp,LlmHelp,PromptTemplatesHelp,Testing,Downloads,TerminalAndFiles,AnnotationDirectory,AnnotationSvgOnly,DateTime,SourceTimeZone,TargetTimeZone,UnsavedChanges,PromptParametersHelp,SuggestedFolders,PipelineCommandHint,MoveUp,MoveDown,InsertStage,AddStage,StageTemplates,TemperatureHelp,SavePng,CopyPayload,AskLlmTitle,CopyClipboardText,AskLlm,AskLlmHelp,AllowPageFetchTitle,AllowPageFetchBody,AllowOnce,AlwaysAllow,FetchingPage,NoKnownPlaceholders,RestoredPlaceholders,PreviewEditApplied,ApplyEdit,RevertEdit,EditHint,Summarize,GroupBy,Operator,Values,FormulaColumn,AddColumn,Constant,ResetTable,NoAggregate,ColumnName,SummarizeHelp,FormulaHelp,HealthOk,HealthDown,HealthChecked,HealthClickToCheck,OptimizePrompt,OptimizingPrompt,PromptOptimized,RevertOptimization,OptimizePromptHelp,PromptOptimizerInstructions,PromptOptimizerInstructionsHelp,Home,HomeTagline,PrincipleHandy,PrincipleHandyBody,PrincipleFast,PrincipleFastBody,PrinciplePipeline,PrinciplePipelineBody,PrincipleCrossPlatform,PrincipleCrossPlatformBody,PrincipleLearns,PrincipleLearnsBody,PrinciplePrivate,PrinciplePrivateBody,HowToUse,HowToUseBody,Providers,NotConfigured,Test,AtAGlance,ClipboardItemsStat,RecentPathsStat,LearnedActionsStat,PromptsStat,RecipesStat,OpenRepository,BuildInfo,
    StylePlaceholder,StyleMask,StyleFake,StyleRedact,NothingFound,Replaced,PrivacyHelp,ReplacementStyle,AnonymizeBeforeLlm,AnonymizeBeforeLlmHelp,AllowPageFetch,Detect,AlwaysHide,NeverHide,CustomCommands,CustomCommandsHelp,AllowAnyProgram,AllowAnyProgramHelp,
    PipelineHelp,ExternalTool,PipelineInput,PipelineOutput,SaveAsRecipe,RecipeName,AllowedTools,AllowedToolsHelp,Recipes,AppliesTo,Command,NewRecipe,PipelinesHelp,
    RecentRowHint,NoNumberColumns,AllRows,ConfigureLlmFirst,LlmNotConfigured,ActionUnavailable,NotATable,SourceGone,MermaidRunning,TransformRunning,SummarizeFailed,PublishFailed,HelpConcepts,HelpGlossary,HelpShortcuts,HelpSearchHint,ShowMe,WhereYouSeeIt,CopyShortcuts,HelpNoMatches,GroupGeneral,GroupCards,GroupEditing,GroupWindows,ShortcutOpen,ShortcutRunCard,ShortcutMove,ShortcutRunSelected,ShortcutSearch,ShortcutTasks,ShortcutHelp,ShortcutShortcuts,ShortcutSettings,ShortcutAskLlm,ShortcutTheme,ShortcutApplyEdit,ShortcutEscape,ShortcutUndoMark,ConceptClipboardTitle,ConceptClipboardBody,ConceptClipboardWhere,ConceptCardsTitle,ConceptCardsBody,ConceptCardsWhere,ConceptRankingTitle,ConceptRankingBody,ConceptRankingWhere,ConceptPreviewTitle,ConceptPreviewBody,ConceptPreviewWhere,ConceptPipelineTitle,ConceptPipelineBody,ConceptPipelineWhere,ConceptPrivacyTitle,ConceptPrivacyBody,ConceptPrivacyWhere,ConceptTasksTitle,ConceptTasksBody,ConceptTasksWhere,TermActionCard,TermActionCardDef,TermAnonymize,TermAnonymizeDef,TermAskLlm,TermAskLlmDef,TermClef,TermClefDef,TermPalette,TermPaletteDef,TermCompare,TermCompareDef,TermCustomCommand,TermCustomCommandDef,TermFallback,TermFallbackDef,TermGlobalShortcut,TermGlobalShortcutDef,TermJev,TermJevDef,TermPipeline,TermPipelineDef,TermPlaceholder,TermPlaceholderDef,TermPreview,TermPreviewDef,TermPromptTemplate,TermPromptTemplateDef,TermRecipe,TermRecipeDef,TermTaskQueue,TermTaskQueueDef,Tasks,Logs,NoTasks,NoLogs,PauseAll,ResumeAll,CancelAll,ClearDone,Remove,TaskQueued,TaskRunning,TaskPaused,TaskDone,TaskFailed,TaskCanceled,StatusReadyLine,StatusBusyLine,StatusOfflineLine,PaletteHint,PaletteNoResults,GroupGoTo,GroupActions,GroupAppearance,GroupHistory,GroupApp,SearchCommand,SwitchTheme,HidePopup,CheckProvidersNow,TaskQueue,HelpCommand,ThemeSystem,NoPreview,Compare,Original,Result,DiffApproximate,DiffLines,PreviewInInput,PreviewResult,RestoreOriginal,CompareResult,Base64EncodeTool,Base64DecodeTool,ResetPrompt,RestoreDefaultTemplates,RestoreDefaultTemplatesHelp,
    Visible,FindReplace,FindText,ReplaceWith,AllColumns,UseRegex,MatchCase,OnlyFilteredRows,ReplaceAll,CellsMatch,CellsReplaced,ReplaceHelp,ColumnType,TypeAuto,TypeText,TypeNumber,TypeDate,HideColumn,UndoChange,ColumnsHelp,ReplaceInColumn,
    Filter,Columns,Statistics,Chart,Copied,Rows,CopyAsSql,CopyAsJson,CopyAsCsv,CopyAsMarkdown,ShowSource,CopyPlainText,CopyHtml,ChartLine,ChartBar,ChartPie,ChartScatter,ChartHistogram,RowNumber,XAxis,YAxis,
    GlobalShortcut,GlobalShortcutHelp,PressShortcut,ChangeShortcut,ShortcutAvailable,ShortcutCurrent,ShortcutInUse,ShortcutUnsupported,ShortcutUnknown,ShortcutBlocked,ShortcutWarning,ShortcutRegisterFailed,ShortcutRegisterStartupFailed
};
UiLanguage resolve_language(UiLanguage requested,std::string_view locale);
// Generated tables (localization_ja_ko.cpp); nullptr when a key has no entry.
const char* tr_japanese(UiTextKey key);
const char* tr_korean(UiTextKey key);
std::string tr(UiLanguage language,UiTextKey key);
// Language for panels that do not receive one explicitly; set once per frame.
void set_active_language(UiLanguage language);
std::string tr(UiTextKey key);
}
