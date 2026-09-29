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
    Filter,Columns,Statistics,Chart,Copied,Rows,CopyAsSql,CopyAsJson,CopyAsCsv,CopyAsMarkdown,ShowSource,CopyPlainText,CopyHtml,ChartLine,ChartBar,ChartPie,ChartScatter,ChartHistogram,RowNumber,XAxis,YAxis,
    GlobalShortcut,GlobalShortcutHelp,PressShortcut,ChangeShortcut,ShortcutAvailable,ShortcutCurrent,ShortcutInUse,ShortcutUnsupported,ShortcutUnknown,ShortcutBlocked,ShortcutWarning,ShortcutRegisterFailed,ShortcutRegisterStartupFailed
};
UiLanguage resolve_language(UiLanguage requested,std::string_view locale);
std::string tr(UiLanguage language,UiTextKey key);
// Language for panels that do not receive one explicitly; set once per frame.
void set_active_language(UiLanguage language);
std::string tr(UiTextKey key);
}
