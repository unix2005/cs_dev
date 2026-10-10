#include <gtk/gtk.h>
#include <cairo.h>
#include <string.h>

#ifndef UI_DIR
#define UI_DIR "./ui"
#endif

static GtkWidget     *login_win = NULL;
static GtkWidget     *main_win  = NULL;
static GtkBuilder    *login_builder = NULL;

/* ---------------- UI 文件加载 ---------------- */
static GtkBuilder *load_ui(const char *file)
{
    char  *path = g_build_filename(UI_DIR, file, NULL);
    GError *err = NULL;
    GtkBuilder *b = gtk_builder_new_from_file(path, &err);
    if (err) {
        g_printerr("无法加载 UI 文件 %s: %s\n", path, err->message);
        g_error_free(err);
    }
    g_free(path);
    return b;
}

/* ---------------- 组织机构树 ---------------- */
enum { ORG_COL_NAME, ORG_COL_TYPE, ORG_COL_ID, ORG_N_COLS };

static void build_org_tree(GtkTreeView *tv)
{
    GtkTreeStore *store = gtk_tree_store_new(ORG_N_COLS,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkTreeIter top, org, veh;

    gtk_tree_store_append(store, &top, NULL);
    gtk_tree_store_set(store, &top,
        ORG_COL_NAME, "集团总部", ORG_COL_TYPE, "org", ORG_COL_ID, "G0", -1);

    gtk_tree_store_append(store, &org, &top);
    gtk_tree_store_set(store, &org,
        ORG_COL_NAME, "华北分公司", ORG_COL_TYPE, "org", ORG_COL_ID, "G1", -1);
    gtk_tree_store_append(store, &veh, &org);
    gtk_tree_store_set(store, &veh,
        ORG_COL_NAME, "京A·12345 运输车", ORG_COL_TYPE, "vehicle", ORG_COL_ID, "V1", -1);
    gtk_tree_store_append(store, &veh, &org);
    gtk_tree_store_set(store, &veh,
        ORG_COL_NAME, "京A·67890 叉车", ORG_COL_TYPE, "vehicle", ORG_COL_ID, "V2", -1);

    gtk_tree_store_append(store, &org, &top);
    gtk_tree_store_set(store, &org,
        ORG_COL_NAME, "华东分公司", ORG_COL_TYPE, "org", ORG_COL_ID, "G2", -1);
    gtk_tree_store_append(store, &veh, &org);
    gtk_tree_store_set(store, &veh,
        ORG_COL_NAME, "沪B·00001 运输车", ORG_COL_TYPE, "vehicle", ORG_COL_ID, "V3", -1);

    GtkCellRenderer     *r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn   *c = gtk_tree_view_column_new_with_attributes(
        "组织机构 / 车辆", r, "text", ORG_COL_NAME, NULL);
    gtk_tree_view_append_column(tv, c);
    gtk_tree_view_set_model(tv, GTK_TREE_MODEL(store));
    g_object_unref(store);
}

/* ---------------- 数据表格 ---------------- */
enum { T_COL_PLATE, T_COL_TYPE, T_COL_STATUS, T_COL_ORG, T_COL_POS, T_N_COLS };

static void build_data_table(GtkTreeView *tv)
{
    GtkListStore *store = gtk_list_store_new(T_N_COLS,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkTreeIter it;
    const char *rows[][5] = {
        {"京A·12345", "运输车", "行驶中", "华北分公司", "北京-朝阳区"},
        {"京A·67890", "叉车",   "闲置",   "华北分公司", "北京-库房"},
        {"沪B·00001", "运输车", "离线",   "华东分公司", "上海-浦东"},
    };
    for (size_t i = 0; i < G_N_ELEMENTS(rows); i++) {
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it,
            T_COL_PLATE,  rows[i][0],
            T_COL_TYPE,   rows[i][1],
            T_COL_STATUS, rows[i][2],
            T_COL_ORG,    rows[i][3],
            T_COL_POS,    rows[i][4], -1);
    }

    const char *titles[T_N_COLS] = {"车牌", "车型", "状态", "所属机构", "位置"};
    for (int i = 0; i < T_N_COLS; i++) {
        GtkCellRenderer   *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(
            titles[i], r, "text", i, NULL);
        gtk_tree_view_column_set_expand(c, TRUE);
        gtk_tree_view_append_column(tv, c);
    }
    gtk_tree_view_set_model(tv, GTK_TREE_MODEL(store));
    g_object_unref(store);
}

/* ---------------- 地图占位绘制 ---------------- */
static void map_draw(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data)
{
    (void)area; (void)data;

    /* 背景 */
    cairo_set_source_rgb(cr, 0.87, 0.90, 0.93);
    cairo_paint(cr);

    /* 网格 */
    cairo_set_source_rgb(cr, 0.78, 0.82, 0.88);
    cairo_set_line_width(cr, 1);
    for (int x = 0; x < w; x += 40) { cairo_move_to(cr, x, 0); cairo_line_to(cr, x, h); }
    for (int y = 0; y < h; y += 40) { cairo_move_to(cr, 0, y); cairo_line_to(cr, w, y); }
    cairo_stroke();

    /* 车辆标记 */
    double pts[][2] = {{120,90}, {300,200}, {520,140}, {420,320}};
    cairo_set_source_rgb(cr, 0.16, 0.42, 0.69);
    for (size_t i = 0; i < G_N_ELEMENTS(pts); i++) {
        cairo_arc(cr, pts[i][0], pts[i][1], 7, 0, 2 * G_PI);
        cairo_fill();
    }

    /* 提示文字 */
    cairo_set_source_rgb(cr, 0.30, 0.30, 0.30);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 14);
    cairo_move_to(cr, 12, 24);
    cairo_show_text(cr, "地图占位（后续接入实时定位）");
}

/* ---------------- 菜单（GMenu）---------------- */
static void act_quit(GSimpleAction *a, GVariant *p, gpointer app)
{ (void)a; (void)p; g_application_quit(G_APPLICATION(app)); }
static void act_refresh(GSimpleAction *a, GVariant *p, gpointer app)
{ (void)a; (void)p; (void)app; g_print("刷新车辆数据\n"); }
static void act_about(GSimpleAction *a, GVariant *p, gpointer app)
{
    (void)a; (void)p;
    GtkWindow *parent = GTK_WINDOW(main_win);
    GtkWidget *dlg = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dlg), "车辆管理系统");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dlg), "v1.0");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dlg), "GTK4 车辆管理客户端");
    gtk_window_set_transient_for(GTK_WINDOW(dlg), parent);
    gtk_window_present(GTK_WINDOW(dlg));
}

static void add_action(GtkApplication *app, const char *name, GCallback cb)
{
    GSimpleAction *a = g_simple_action_new(name, NULL);
    g_signal_connect(a, "activate", cb, app);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(a));
}

static void build_menubar(GtkMenuButton *btn, GtkApplication *app)
{
    GMenu *menu = g_menu_new();

    GMenu *m_file = g_menu_new();
    g_menu_append(m_file, "退出", "app.quit");
    g_menu_append_section(menu, "文件", G_MENU_MODEL(m_file));

    GMenu *m_view = g_menu_new();
    g_menu_append(m_view, "刷新", "app.refresh");
    g_menu_append_section(menu, "视图", G_MENU_MODEL(m_view));

    GMenu *m_help = g_menu_new();
    g_menu_append(m_help, "关于", "app.about");
    g_menu_append_section(menu, "帮助", G_MENU_MODEL(m_help));

    gtk_menu_button_set_menu_model(btn, G_MENU_MODEL(menu));
}

/* ---------------- 登录回调 ---------------- */
static void on_login_clicked(GtkButton *btn, gpointer data)
{
    (void)btn; (void)data;
    GtkEntry  *euser = GTK_ENTRY(gtk_builder_get_object(login_builder, "entry_user"));
    GtkWidget *epass = GTK_WIDGET(gtk_builder_get_object(login_builder, "entry_pass"));
    GtkLabel  *msg   = GTK_LABEL(gtk_builder_get_object(login_builder, "login_msg"));

    const char *u = gtk_editable_get_text(GTK_EDITABLE(euser));
    const char *p = gtk_editable_get_text(GTK_EDITABLE(epass));

    if (u && *u && p && *p) {
        gtk_window_destroy(GTK_WINDOW(login_win));
        gtk_window_present(GTK_WINDOW(main_win));
    } else {
        gtk_label_set_text(msg, "请输入用户名和密码");
    }
}

/* ---------------- 激活 ---------------- */
static void activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    /* 样式 */
    char *cpath = g_build_filename(UI_DIR, "style.css", NULL);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_file(css, g_file_new_for_path(cpath), NULL);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_free(cpath);

    /* 应用级动作 */
    add_action(app, "quit",    G_CALLBACK(act_quit));
    add_action(app, "refresh", G_CALLBACK(act_refresh));
    add_action(app, "about",   G_CALLBACK(act_about));

    /* 登录窗口 */
    login_builder = load_ui("login.ui");
    g_assert(login_builder != NULL);
    login_win = GTK_WIDGET(gtk_builder_get_object(login_builder, "login_window"));
    GtkWidget *btn = GTK_WIDGET(gtk_builder_get_object(login_builder, "btn_login"));
    g_signal_connect(btn, "clicked", G_CALLBACK(on_login_clicked), NULL);

    /* 主窗口 */
    GtkBuilder *mb = load_ui("main.ui");
    g_assert(mb != NULL);
    main_win = GTK_WIDGET(gtk_builder_get_object(mb, "main_window"));
    gtk_window_set_application(GTK_WINDOW(main_win), app);

    GtkWidget *org_tree  = GTK_WIDGET(gtk_builder_get_object(mb, "org_tree"));
    GtkWidget *data_tbl  = GTK_WIDGET(gtk_builder_get_object(mb, "data_table"));
    GtkWidget *map_area  = GTK_WIDGET(gtk_builder_get_object(mb, "map_area"));
    GtkWidget *menubtn   = GTK_WIDGET(gtk_builder_get_object(mb, "menubtn"));

    build_org_tree(GTK_TREE_VIEW(org_tree));
    build_data_table(GTK_TREE_VIEW(data_tbl));
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(map_area), map_draw, NULL, NULL);
    build_menubar(GTK_MENU_BUTTON(menubtn), app);

    gtk_window_present(GTK_WINDOW(login_win));
}

int main(int argc, char *argv[])
{
    GtkApplication *app = gtk_application_new("com.gtkvms.client", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
