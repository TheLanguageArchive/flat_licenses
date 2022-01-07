<?php
/**
 * Building table for assigning licenses to pids
 *
 * @param array $variables
 */
function theme_flat_licenses_admin_table(&$variables)
{

    $form = $variables['form'];
    $rows = [];

    foreach (element_children($form['licenses']) as $id) {
        $rows[] = [

            ['data' => drupal_render($form['licenses'][$id]['label']),         'class' => 'flat-licenses-label'],
            ['data' => drupal_render($form['licenses'][$id]['url']),           'class' => 'flat-licenses-url'],
            ['data' => drupal_render($form['licenses'][$id]['pid']),         'class' => 'flat-licenses-pid'],
            ['data' => drupal_render($form['licenses'][$id]['operations']),   'class' => 'flat-licenses-operations'],
        ];
    }

    $header = [

        ['data' => 'LABEL',     'class' => 'flat-licenses-label-render-header'],
        ['data' => 'URL',          'class' => 'flat-licenses-url-render-header'],
        ['data' => 'FEDORA PID',          'class' => 'flat-licenses-pid-render-header'],
        ['data' => 'OPERATIONS',     'class' => 'flat-licenses-operations-render-header'],
    ];

    $controls = [[

        'data'        => [['data' => drupal_render($form['add_license']), 'colspan' => 5]],
        'class'       => ['add-item-controls'],
        'no_striping' => 1,
    ]];

    $output = theme('table', [

        'header'     => $header,
        'rows'       => $rows,
        'attributes' => [

            'id'    => 'flat-licenses-overview',
            'class' => ['flat-licenses-table']
        ]
    ]);

    $output .= theme('table', [

        'header'     => [],
        'rows'       => $controls,
        'attributes' => ['class' => ['flat-licenses-table-controls']],
    ]);

    return $output;
}
