<?php
// $Id: workflow.admin.inc,v 1.7.2.2 2010/03/02 18:32:54 jvandyk Exp $

/**
 * @file
 * Administrative pages for configuring workflows.
 */

/**
 * Form builder. Create the form for adding a workflow.
 */
function workflow_add_form(&$form_state) {
  $form = array();
  $form['label'] = array(
    '#type' => 'textfield',
    '#title' => t('Workflow Label'),
    '#required' => TRUE,
    '#maxlength' => '254',
  );
  $form['name'] = array(
    '#type' => 'textfield',
    '#title' => t('Machine Name'),
    '#required' => TRUE,
    '#description' => t('Enter the machine name for this workflow. Must be unique and use only alphanumeric characters and underscores.'),
    '#maxlength' => '254',
  );
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Add Workflow')
  );
  return $form;
}

/**
 * Validate the workflow add form.
 *
 * @see workflow_add_form()
 */
function workflow_add_form_validate($form, &$form_state) {
  $form_state['values']['name'] = strtolower($form_state['values']['name']);
  $values = $form_state['values'];

  // Make sure workflow name is not a duplicate.
  if (db_result(db_query("SELECT COUNT(*) FROM {workflows} WHERE label = '%s'", $values['label']))) {
    form_set_error('label', t('A workflow with the name %name already exists. Please enter another name for your new workflow.',
      array('%name' => $values['label'])));
  }

  // Make sure machine name contains only alnum chars and underscores.
  if (preg_match("/[^[:alnum:]_]/", $values['name'])) {
    form_set_error('name', t("The machine name must consist of alphanumeric or underscore characters only."));
  }
  // Make sure workflow name is not a duplicate.
  if (db_result(db_query("SELECT COUNT(*) FROM {workflows} WHERE name = '%s'", $values['name']))) {
    form_set_error('name', t('A workflow with the machine name %name already exists. Please enter another name for your new workflow.',
      array('%name' => $values['name'])));
  }
  // Make sure the workflow name isn't associated with disabled states.
  if (db_result(db_query("SELECT COUNT(*) FROM {workflow_states} WHERE workflow_name = '%s'", $values['name']))) {
    form_set_error('name', t('It appears this machine name has previously disabled states. Please enter another name for your new workflow.',
      array('%name' => $values['name'])));
  }
}

/**
 * Submit handler for the workflow add form.
 *
 * @see workflow_add_form()
 */
function workflow_add_form_submit($form, &$form_state) {
  $values = $form_state['values'];

  $workflow['label'] = $values['label'];
  $workflow['name'] = strtolower($values['name']);

  if ($workflow = workflow_create($workflow)) {
    watchdog('workflow', 'Created workflow %label', array('%label' => $workflow->label));
    drupal_set_message(t('The workflow %label was created. You should now add states to your workflow.', array('%label' => $workflow->label)), 'status');
    $form_state['redirect'] = 'admin/build/workflow/state/'. $workflow->name;
  }
  else {
    watchdog('workflow', 'There was an error creating workflow %label', array('%label' => $values['label']), WATCHDOG_WARNING);
    drupal_set_message(t('There was a problem saving this workflow. Please contact your site administrator.'), 'warning');
  }
}

/**
 * Form for confirmation of workflow deletion.
 *
 * @param $name
 *   The machine name of the workflow to delete.
 * @return
 *   Confirmation form array.
 */
function workflow_delete_form(&$form_state, $name) {
  $form['name'] = array(
    '#type' => 'value',
    '#value' => $name,
  );
  return confirm_form(
    $form,
    t('Are you sure you want to delete %title? All nodes that have a workflow state associated with this workflow will have those workflow states removed.', array('%title' => workflow_get_label($name))),
    !empty($_GET['destination']) ? $_GET['destination'] : 'admin/build/workflow',
    t('This action cannot be undone.'),
    t('Delete'),
    t('Cancel')
  );
}

/**
 * Submit handler for workflow deletion form.
 *
 * @see workflow_delete_form()
 */
function workflow_delete_form_submit($form, &$form_state) {
  if ($form_state['values']['confirm'] == 1) {
    $name = $form_state['values']['name'];
    $label = workflow_get_label($name);
    workflow_deletewf($name);
    watchdog('workflow', 'Deleted workflow %label with all its states', array('%label' => $label));
    drupal_set_message(t('The workflow %label with all its states was deleted.', array('%label' => $label)));
    $form_state['redirect'] = 'admin/build/workflow';
  }
}

/**
 * View workflow permissions by role
 *
 * @param $name
 *   The machine name of the workflow.
 */
function workflow_permissions($name) {
  $all = array();
  $roles = array('author' => t('author')) + user_roles();
  foreach ($roles as $role => $value) {
    $all[$role]['name'] = $value;
  }
  $result = db_query(
    'SELECT t.roles, s1.label AS state_name, s2.label AS target_state_name ' .
    'FROM {workflow_transitions} t ' .
    'INNER JOIN {workflow_states} s1 ON s1.sid = t.sid '.
    'INNER JOIN {workflow_states} s2 ON s2.sid = t.target_sid '.
    "WHERE s1.workflow_name = '%s' " .
    'ORDER BY s1.weight ASC , s1.label ASC , s2.weight ASC , s2.label ASC',
    $name);

  while ($data = db_fetch_object($result)) {
    foreach (explode(',', $data->roles) as $role) {
      $all[$role]['transitions'][] = array(check_plain(t($data->state_name)), WORKFLOW_ARROW, check_plain(t($data->target_state_name)));
    }
  }

  $header = array(t('From'), '', t('To'));
  return theme('workflow_permissions', $header, $all);
}

/**
 * Theme the workflow permissions view.
 */
function theme_workflow_permissions($header, $all) {
  $output = '';
  foreach ($all as $role => $value) {
    $output .= '<h3>'. t("%role may do these transitions:", array('%role' => $value['name'])) .'</h3>';
    if (!empty($value['transitions'])) {
      $output .= theme('table', $header, $value['transitions']) . '<p></p>';
    }
    else {
      $output .= '<table><tbody><tr class="odd"><td>' . t('None') . '</td><td></tr></tbody></table><p></p>';
    }
  }

  return $output;
}

/**
 * Menu callback. Edit a workflow's properties.
 *
 * @param $workflow
 *   The workflow object.
 * @return
 *   HTML form and permissions table.
 */
function workflow_edit_form($form_state, $workflow) {
  $form['#workflow'] = $workflow;
  $form['wid'] = array(
    '#type' => 'value',
    '#value' => $workflow->wid,
  );
  $form['name'] = array(
    '#type' => 'value',
    '#value' => $workflow->name,
  );
  $form['basic'] = array(
    '#type' => 'fieldset',
    '#title' => t('Workflow information')
  );
  $form['basic']['label'] = array(
    '#type' => 'textfield',
    '#default_value' => $workflow->label,
    '#title' => t('Workflow Label'),
    '#size' => '16',
    '#maxlength' => '254',
  );
  $form['comment'] = array(
    '#type' => 'fieldset',
    '#title' => t('Comment for Workflow Log'),
  );
  $form['comment']['comment_log_node'] = array(
    '#type' => 'checkbox',
    '#title' => t('Show a comment field in the workflow section of the editing form.'),
    '#default_value' => $workflow->options['comment_log_node'],
    '#description' => t("On the node editing form, a Comment form can be shown so that the person making the state change can record reasons for doing so. The comment is then included in the node's workflow history."),
  );
  $form['comment']['comment_log_tab'] = array(
    '#type' => 'checkbox',
    '#title' => t('Show a comment field in the workflow section of the workflow tab form.'),
    '#default_value' => $workflow->options['comment_log_tab'],
    '#description' => t("On the workflow tab, a Comment form can be shown so that the person making the state change can record reasons for doing so. The comment is then included in the node's workflow history."),
  );
  $form['tab'] = array(
    '#type' => 'fieldset',
    '#title' => t('Workflow tab permissions'),
    '#collapsible' => TRUE,
    '#collapsed' => FALSE,
  );
  $form['tab']['tab_roles'] = array(
    '#type' => 'checkboxes',
    '#options' => workflow_get_roles(),
    '#default_value' => explode(',', $workflow->tab_roles),
    '#description' => t('Select any roles that should have access to the workflow tab on nodes that have a workflow.'),
  );

  $form['transitions'] = workflow_transition_grid_form($workflow->name);
  $form['transitions']['#tree'] = TRUE;

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save')
  );

  $form['permissions'] = array(
    '#type' => 'fieldset',
    '#title' => t('Permissions Summary'),
    '#collapsible' => TRUE,
  );
  $form['permissions']['summary'] = array(
    '#value' => workflow_permissions($workflow->name),
  );

  return $form;
}

/**
 * Theme the workflow editing form.
 *
 * @see workflow_edit_form()
 */
function theme_workflow_edit_form($form) {
  $output = drupal_render($form['label']);
  $machine_name = $form['name']['#value'];
  $states = workflow_get_states($machine_name, 'sid');
  drupal_set_title(t('Edit workflow %label', array('%label' => workflow_get_label($machine_name))));
  if ($states) {
    $roles = workflow_get_roles();
    $header = array(array('data' => t('From / To ') . '&nbsp;' . WORKFLOW_ARROW));
    $rows = array();
    foreach ($states as $state_id => $state) {
      // Don't allow transition TO (creation).
      if (!workflow_is_system_state($state->label)) {
        $header[] = array('data' => $state->label);
      }
      $row = array(array('data' => $state->label));
      foreach ($states as $nested_state_id => $nested_state) {
        if (workflow_is_system_state($nested_state->label)) {
          // Don't allow transition TO (creation).
          continue;
        }
        if ($nested_state_id != $state_id) {
          // Need to render checkboxes for transition from $state to $nested_state.
          $from = $state_id;
          $to = $nested_state_id;
          $cell = '';
          foreach ($roles as $rid => $role_name) {
            $cell .= drupal_render($form['transitions'][$from][$to][$rid]);
          }
          $row[] = array('data' => $cell);
        }
        else {
          $row[] = array('data' => '');
        }
      }
      $rows[] = $row;
    }
    $output .= theme('table', $header, $rows);

  }
  else {
    $output = t('There are no states defined for this workflow.');
  }

  $output .= drupal_render($form);
  return $output;
}

/**
 * Validate the workflow editing form.
 *
 * @see workflow_edit_form()
 */
function workflow_edit_form_validate($form_id, $form_state) {
  $machine_name = $form_state['values']['name'];
  // Make sure workflow name is not a duplicate.
  if (array_key_exists('label', $form_state['values']) && $form_state['values']['label'] != '') {
    $label = check_plain(t($form_state['values']['label']));
    foreach (workflow_get_all() as $name => $workflow) {
      if ($name != $machine_name && $label == $workflow->label) {
        form_set_error('label', t('A workflow with the label %label already exists. Please enter another label for this workflow.',
          array('%label' => $label)));
        break;
      }
    }
  }
  else {
  // No workflow name was provided.
    form_set_error('label', t('Please provide a nonblank label for this workflow.'));
  }

  // Make sure 'author' is checked for (creation) -> [something].
  $creation_id = _workflow_creation_state($machine_name);
  if (isset($form_state['values']['transitions'][$creation_id]) && is_array($form_state['values']['transitions'][$creation_id])) {
    foreach ($form_state['values']['transitions'][$creation_id] as $to => $roles) {
      if ($roles['author']) {
        $author_has_permission = TRUE;
        break;
      }
    }
  }
  $state_count = db_result(db_query("SELECT COUNT(sid) FROM {workflow_states} WHERE workflow_name = '%s'", $machine_name));
  if (empty($author_has_permission) && $state_count > 1) {
    form_set_error('transitions', t('Please give the author permission to go from %creation to at least one state!',
      array('%creation' => '(creation)')));
  }
}

/**
 * Submit handler for the workflow editing form.
 *
 * @see workflow_edit_form()
 */
function workflow_edit_form_submit($form, &$form_state) {
  if (isset($form_state['values']['transitions'])) {
    workflow_update_transitions($form_state['values']['transitions']);
  }
  workflow_update($form_state['values']['wid'], $form_state['values']['label'], array_filter($form_state['values']['tab_roles']), array('comment_log_node' => $form_state['values']['comment_log_node'], 'comment_log_tab' => $form_state['values']['comment_log_tab']));
  drupal_set_message(t('The workflow was updated.'));
  $form_state['redirect'] = 'admin/build/workflow';
}

/**
 * Menu callback and form builder. Workflow create/edit form.
 *
 * @param $workflow_name
 *   The machine name of the workflow.
 * @param $state_name
 *   The machine name of the state.
 * @return
 *   Form array.
 */
function workflow_state_form(&$form_state, $workflow_name, $state_name = NULL) {
  $form['wid'] = array('#type' => 'value', '#value' => $wid);

  if (!isset($workflow_name)) {
    drupal_set_message(t('You must first select a workflow to add a state to.'));
    drupal_goto('admin/build/workflow');
  }

  if (isset($state_name)) {
    $state = workflow_get_state($state_name);
    $form['#state'] = $state;

    // Don't allow users to edit a system state.
    if (workflow_is_system_state($state->label)) {
      return drupal_access_denied();
    }
  }

  $form['workflow_name'] = array(
    '#type' => 'value',
    '#value' => $workflow_name,
  );

  $form['label'] = array(
    '#type'  => 'textfield',
    '#title' => t('State label'),
    '#default_value' => empty($state->label) ? '' : $state->label,
    '#size'  => '16',
    '#maxlength' => '254',
    '#required' => TRUE,
    '#description' => t('Enter the name for a state in your workflow. For example, if you were doing a meal workflow it may include states like <em>shop</em>, <em>prepare</em>, <em>eat</em>, and <em>clean up</em>.'),
  );
  $form['name'] = array(
    '#type'  => 'textfield',
    '#title' => t('Machine name'),
    '#default_value' => empty($state->name) ? '' : $state->name,
    '#size'  => '16',
    '#maxlength' => '254',
    '#required' => TRUE,
    '#disabled' => !empty($state->name),
    '#description' => t('Enter the machine name for this state. Must be unique and use only alphanumeric characters and underscores.'),
  );

  // Set the title and form based on whether we editing or creating.
  if (!isset($state) || $state === FALSE) {
    drupal_set_title(t('Add a new state to workflow %workflow', array('%workflow' => workflow_get_label($workflow_name))));
  }
  else {
    drupal_set_title(t('Edit workflow state @state', array('@state' => $state->label)));
  }

  $form['weight'] = array(
    '#type' => 'weight',
    '#title' => t('Weight'),
    '#default_value' => empty($state->weight) ? 0 : $state->weight,
    '#description' => t('In listings, the heavier states will sink and the lighter states will be positioned nearer the top.'),
  );
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save')
  );

  return $form;
}

/**
 * Validate the state addition form.
 *
 * @see workflow_state_form()
 */
function workflow_state_form_validate($form, &$form_state) {
  $label = $form_state['values']['label'];
  $machine_name = $form_state['values']['name'];
  $workflow_name = $form_state['values']['workflow_name'];
  $wf_states = workflow_get_states($workflow_name);

  // Make sure name is not a duplicate
  foreach($wf_states as $state) {
    if ($state->label == $label) {
      form_set_error('label', t('A state with the label %state already exists in this workflow. Please enter another name for your new state.', array('%state' => $label)));
    }
    break;
  }

  if (empty($form['#state'])) {
    // Make sure machine name contains only alnum chars and underscores.
    if (preg_match("/[^[:alnum:]_]/", $machine_name)) {
      form_set_error('name', t("The machine name must consist of alphanumeric or underscore characters only."));
    }

    // Ensure that this machine name is unique.
    if (db_result(db_query("SELECT count(*) FROM {workflow_states} WHERE name = '%s'", $machine_name))) {
      form_set_error('name', t('A state with the machine name %name already exists. You might try pre-fixing it with the machine name of the workflow, such as %example.', array('%name' => $machine_name, '%example' => $workflow_name .'_'. $machine_name)));
    }
  }
}

/**
 * Submit handler for state addition form.
 *
 * @see workflow_state_form()
 */
function workflow_state_form_submit($form, &$form_state) {
  $state = workflow_state_save($form_state['values']);
  if (array_key_exists('sid', $form_state['values'])) {
    drupal_set_message(t('The workflow state was updated.'));
  }
  else {
    watchdog('workflow', 'Created workflow state %label', array('%label' => $state->label));
    drupal_set_message(t('The workflow state %label was created.', array('%label' => $state->label)));
  }
  $form_state['redirect'] = 'admin/build/workflow';
}

/**
 * Form builder. Build the grid of transitions for defining a workflow.
 *
 * @param $machine_name
 *   The machine name of the workflow.
 */
function workflow_transition_grid_form($machine_name) {
  $form = array();

  $roles = workflow_get_roles();
  $states = workflow_get_states($machine_name, 'sid');
  if (!$states) {
    $form = array(
      '#type' => 'markup',
      '#value' => t('There are no states defined for this workflow.')
    );
    return $form;
  }
  foreach ($states as $state_id => $state) {
    foreach ($states as $nested_state_id => $nested_state) {
      if (workflow_is_system_state($nested_state->label)) {
        // Don't allow transition TO (creation).
        continue;
      }
      if ($nested_state_id != $state_id) {
        // Need to generate checkboxes for transition from $state to $nested_state.
        $from = $state_id;
        $to = $nested_state_id;
        foreach ($roles as $rid => $role_name) {
          $tid = workflow_get_transition_id($from, $to);
          $form[$from][$to][$rid] = array(
            '#type' => 'checkbox',
            '#title' => check_plain($role_name),
            '#default_value' => $tid ? workflow_transition_allowed($tid, $rid) : FALSE);
        }
      }
    }
  }
  return $form;
}

/**
 * Menu callback. Create the main workflow page, which gives an overview
 * of workflows and workflow states.
 */
function workflow_overview() {
  $row = array();

  foreach (workflow_get_all() as $workflow_name => $workflow) {
    $links = array(
      'workflow_overview_add_state' => array(
        'title' => t('Add state'),
        'href' => "admin/build/workflow/state/$workflow_name"),
      'workflow_overview_actions' => array(
        'title' => t('Actions'),
        'href' => "admin/build/trigger/workflow/$workflow_name"),
      'workflow_overview_edit' => array(
        'title' => t('Edit'),
        'href' => "admin/build/workflow/edit/$workflow_name"),
      'workflow_overview_delete' => array(
        'title' => t('Delete'),
        'href' => "admin/build/workflow/delete/$workflow_name"),
    );

    // Allow modules to insert their own workflow operations.
    $links = array_merge($links, module_invoke_all('workflow_operations', 'workflow', $workflow->wid));

    $states = workflow_get_states($workflow_name);
    if (!module_exists('trigger') || count($states) < 2) {
      unset($links['workflow_overview_actions']);
    }

    $row[] = array(check_plain(t($workflow->label)), theme('links', $links));
    $subrows = array();

    foreach ($states as $state_name => $state) {
      $state_links = array();
      if (!workflow_is_system_state($state->label)) {
        $state_links = array(
          'workflow_overview_edit_state' => array(
            'title' => t('Edit'),
            'href' => "admin/build/workflow/state/$workflow_name/$state_name",
          ),
          'workflow_overview_delete_state' => array(
            'title' => t('Delete'),
            'href' => "admin/build/workflow/state/delete/$workflow_name/$state_name"
          ),
        );
      }
      // Allow modules to insert state operations.
      $state_links = array_merge($state_links, module_invoke_all('workflow_operations', 'state', $workflow->wid, $state->sid));
      $subrows[] = array($state->label, theme('links', $state_links));
      unset($state_links);
    }

    $subheader_state      = array('data' => t('State'), 'style' => 'width: 30%');
    $subheader_operations = array('data' => t('Operations'), 'style' => 'width: 70%');
    $subheader_style      = array('style' => 'width: 100%; margin: 3px 20px 20px;');
    $subtable = theme('table', array($subheader_state, $subheader_operations), $subrows, $subheader_style);

    $row[] = array(array('data' => $subtable, 'colspan' => '2'));
  }

  if ($row) {
    $output = theme('table', array(t('Workflow'), t('Operations')), $row);
    $output .= drupal_get_form('workflow_types_form');
  }
  else {
    $output = '<p>' . t('No workflows have been added. Would you like to <a href="@link">add a workflow</a>?', array('@link' => url('admin/build/workflow/add'))) . '</p>';
  }

  return $output;
}

/**
 * Form for confirmation of deleting a workflow state.
 *
 * @param $workflow_name
 *   The machine name of the workflow.
 * @param $name
 *   The machine name of the state to delete.
 * @return
 *   Confirmation form array.
 */
function workflow_state_delete_form($form_state, $workflow_name, $name) {
  $states = $form['#states'] = workflow_get_state_labels($workflow_name);
  $label = $states[$name];

  // Don't allow users to delete a system state.
  if (workflow_is_system_state($label)) {
    return drupal_access_denied();
  }

  // Will any nodes have no state if this state is deleted?
  if ($count = db_result(db_query("SELECT COUNT(nid) FROM {workflow_node} WHERE sid = %d", $state->sid))) {
    // Cannot assign a node to (creation) because that implies
    // that the node does not exist.
    foreach ($states as $key => $value) {
      if ($value->label == t('(creation)')) {
        unset($states[$key]);
      }
    }

    // Don't include the state to be deleted in our list of possible
    // states that can be assigned.
    unset($states[$name]);
    if ($states) {
      $form['new_state'] = array(
        '#type' => 'select',
        '#title' => t('State to be assigned to orphaned nodes'),
        '#description' => format_plural($count, 'Since you are deleting a workflow state, a node which is in that state will be orphaned, and must be reassigned to a new state. Please choose the new state.', 'Since you are deleting a workflow state, @count nodes which are in that state will be orphaned, and must be reassigned to a new state. Please choose the new state.'),
        '#options' => $states,
      );
    }
    else {
      $form['warning'] = array(
        '#value' => format_plural($count, 'Since you are deleting the last workflow state in this workflow, the one remaining node which is in that state will have its workflow state removed. ', 'Since you are deleting the last workflow state in this workflow, @count nodes which are in that state will have their workflow state removed. '),
      );
    }
  }

  $form['workflow_name'] = array(
    '#type' => 'value',
    '#value' => $workflow_name,
  );
  $form['name'] = array(
    '#type' => 'value',
    '#value' => $name,
  );

  return confirm_form(
    $form,
    t('Are you sure you want to delete %label (and all its transitions)?', array('%label' => $label)),
    !empty($_GET['destination']) ? $_GET['destination'] : 'admin/build/workflow',
    t('This action cannot be undone.'),
    t('Delete'),
    t('Cancel')
  );
}

/**
 * Submit handler for workflow state deletion form.
 *
 * @see workflow_state_delete_form()
 */
function workflow_state_delete_form_submit($form, &$form_state) {
  $states = $form['#states'];
  $name = $form_state['values']['name'];
  $label = $states[$name];
  if ($form_state['values']['confirm'] == 1) {
    $new_state = isset($form_state['values']['new_state']) ? $form_state['values']['new_state'] : NULL;
    workflow_state_delete($name, $new_state);
    watchdog('workflow', 'Deleted workflow state %label', array('%label' => $label));
    drupal_set_message(t('The workflow state %label was deleted.', array('%label' => $label)));
  }
  $form_state['redirect'] = 'admin/build/workflow';
}

/**
 * Form builder. Allow administrator to map workflows to content types
 * and determine placement.
 */
function workflow_types_form() {
  $form = array();
  $workflows = workflow_get_all_labels();
  if (count($workflows) == 0) {
    return $form;
  }

  $options = array('<'. t('None') .'>') + $workflows;

  $type_map = array();
  $result = db_query("SELECT * FROM {workflow_type_map}");
  while ($data = db_fetch_object($result)) {
    $type_map[$data->type] = $data->workflow_name;
  }

  $form['#theme'] = 'workflow_types_form';
  $form['#tree'] = TRUE;
  $form['help'] = array(
    '#type' => 'item',
    '#value' => t('Each content type may have a separate workflow. The form for changing workflow state can be displayed when editing a node, editing a comment for a node, or both.'),
  );

  foreach (node_get_types('names') as $type => $name) {
    $form[$type]['workflow_name'] = array(
      '#type' => 'select',
      '#title' => check_plain($name),
      '#options' => $options,
      '#default_value' => isset($type_map[$type]) ? $type_map[$type] : 0
    );
    $form[$type]['placement'] = array(
      '#type' => 'checkboxes',
      '#options' => array(
        'node' => t('Post'),
        'comment' => t('Comment'),
      ),
      '#default_value' => variable_get('workflow_' . $type, array('node')),
    );
  }
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save workflow mapping')
  );
  return $form;
}

/**
 * Theme the workflow type mapping form.
 */
function theme_workflow_types_form($form) {
  $header = array(t('Content Type'), t('Workflow'), t('Display Workflow Form for:'));
  $rows = array();
  foreach (node_get_types('names') as $type => $name) {
    $name = $form[$type]['workflow_name']['#title'];
    unset($form[$type]['workflow_name']['#title']);
    $rows[] = array($name, drupal_render($form[$type]['workflow_name']), drupal_render($form[$type]['placement']));
  }
  $output = drupal_render($form['help']);
  $output .= theme('table', $header, $rows);
  return $output . drupal_render($form);
}

/**
 * Submit handler for workflow type mapping form.
 *
 * @see workflow_types_form()
 */
function workflow_types_form_submit($form, &$form_state) {
  workflow_types_save($form_state['values']);
  drupal_set_message(t('The workflow mapping was saved.'));
  menu_rebuild();
  $form_state['redirect'] = 'admin/build/workflow';
}